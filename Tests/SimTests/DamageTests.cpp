#include "pch.h"

#include "Damage.h"

#include "DamageTable.h"

#include <cstdint>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The damage formula of GameDesign.md §8, against values worked out by hand from the version-1
// matrix the design prints (m1-vertical-slice/S10). The formula itself lives in Content, because
// the design screen and Tools/CheckBalance.py apply the same one; what is pinned here is that the
// simulation reaches the design's own numbers through it, columns and armour kinds included.
namespace SimTests
{

namespace
{

/// The version-1 matrix of GameDesign.md §8, transcribed from the design rather than loaded, so
/// that a table edited in GameData fails ContentTests against the design and this suite goes on
/// measuring the formula. Rows are the five weapon classes; columns the six drives then the four
/// strengths.
const Frontier::DamageTable& Version1()
{
  static const Frontier::DamageTable TABLE = []
  {
    Frontier::DamageTable table{};
    table.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::AntiLight)] = {120, 100, 50, 110, 130, 100, 120, 60, 30, 20};
    table.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::AntiTank)] = {90, 100, 120, 90, 70, 60, 80, 100, 110, 60};
    table.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Flame)] = {130, 110, 70, 120, 140, 40, 150, 80, 40, 10};
    table.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Artillery)] = {100, 100, 100, 100, 100, 20, 130, 120, 100, 60};
    table.modifierPercent[static_cast<std::size_t>(Frontier::WeaponClass::Energy)] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 80};
    table.armorFactorPercent = {100, 100, 100, 0, 50};
    table.armorKind = {Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Thermal,
                       Frontier::ArmorKind::Kinetic, Frontier::ArmorKind::Kinetic};
    return table;
  }();
  return TABLE;
}

constexpr std::uint8_t TRACKS = Frontier::TargetColumnOf(Frontier::DriveClass::Tracks);

} // namespace

TEST_CLASS(DamageTests)
{
public:
  TEST_METHOD(ACannonAgainstTracksBehindTwentyFiveArmourDealsTheDesignsFortySeven)
  {
    // The worked example of m1-vertical-slice/S10's acceptance: 60 damage, anti-tank against
    // tracks is 120 percent, so 72 scaled; anti-tank meets all of the armour, so 72 - 25.
    Assert::AreEqual(47, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiTank, TRACKS, 60, 25));
  }

  TEST_METHOD(ATHirdOfTheScaledDamageIsTheFloorHoweverThickTheArmour)
  {
    // A machine gun against tracks: 8 damage at 50 percent is 4 scaled, and 25 armour would take
    // it well past nothing. GameDesign.md §8's floor is what keeps a weapon from being useless
    // rather than merely poor, and it is a third of the SCALED damage and not of the raw.
    Assert::AreEqual(1, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiLight, TRACKS, 8, 25));
    Assert::AreEqual(1, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiLight, TRACKS, 8, 1000), L"and it does not go under");
    // Where the armour does not reach the floor, the subtraction is what is dealt.
    Assert::AreEqual(3, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiLight, TRACKS, 8, 1));
  }

  TEST_METHOD(ArtilleryMeetsNoArmourAndEnergyMeetsHalfOfIt)
  {
    // Both at 100 percent against tracks, so the only difference is the armour factor: artillery
    // is indifferent to armour and the laser ignores half of it (GameDesign.md §8).
    Assert::AreEqual(80, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::Artillery, TRACKS, 80, 25));
    Assert::AreEqual(68, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::Energy, TRACKS, 80, 25));
    // And anti-tank meets all of it, at 120 percent: 96 scaled, less 25, is 71.
    Assert::AreEqual(71, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiTank, TRACKS, 80, 25));
  }

  TEST_METHOD(TheColumnsAreTheSixDrivesAndThenTheFourStrengths)
  {
    Assert::AreEqual(0, static_cast<int>(Frontier::TargetColumnOf(Frontier::DriveClass::Wheels)));
    Assert::AreEqual(5, static_cast<int>(Frontier::TargetColumnOf(Frontier::DriveClass::Lift)));
    Assert::AreEqual(6, static_cast<int>(Frontier::TargetColumnOf(Frontier::StrengthClass::Soft)));
    Assert::AreEqual(9, static_cast<int>(Frontier::TargetColumnOf(Frontier::StrengthClass::Bunker)));
    Assert::AreEqual(10, static_cast<int>(Frontier::TARGET_CLASS_COUNT));
    // A bunker is what the design says it is: anti-light is nearly useless against it, anti-tank
    // and artillery are the answer.
    Assert::AreEqual(1, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiLight,
                                              Frontier::TargetColumnOf(Frontier::StrengthClass::Bunker), 8, 0));
    Assert::AreEqual(36, Frontier::DamageDealt(Version1(), Frontier::WeaponClass::AntiTank,
                                               Frontier::TargetColumnOf(Frontier::StrengthClass::Bunker), 60, 0));
  }

  TEST_METHOD(ArmorAgainstPicksTheKindTheWeaponClassMeets)
  {
    const Frontier::TargetArmor armor{TRACKS, 25, 8};
    Assert::AreEqual(25, Frontier::ArmorAgainst(Version1(), Frontier::WeaponClass::AntiTank, armor));
    Assert::AreEqual(25, Frontier::ArmorAgainst(Version1(), Frontier::WeaponClass::Artillery, armor));
    Assert::AreEqual(25, Frontier::ArmorAgainst(Version1(), Frontier::WeaponClass::Energy, armor));
    Assert::AreEqual(8, Frontier::ArmorAgainst(Version1(), Frontier::WeaponClass::Flame, armor), L"flame meets the thermal armour");
  }
};

} // namespace SimTests
