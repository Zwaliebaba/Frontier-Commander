#include "pch.h"

#include "Messages.h"
#include "Records.h"

#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The wire (TechnicalDesign.md §5.3; m1-vertical-slice/N1). Three things are asserted here and
// they are different things: that one instance of every record comes out as EXACTLY these bytes,
// so that a layout change is a diff a reviewer sees rather than a protocol break a player finds;
// that every record and every message survives a round trip; and that a reader hands back false
// rather than a half-read record for a stream that is short, or that names a value no enumeration
// has, or that claims more records than it carries.
namespace NetTests
{

namespace
{

/// The bytes a record writes, on their own.
template <class T> std::vector<std::byte> Bytes(const T& _record)
{
  Neuron::ByteWriter writer;
  Frontier::Write(writer, _record);
  return std::vector<std::byte>(writer.Bytes().begin(), writer.Bytes().end());
}

void AssertBytes(const std::vector<std::byte>& _written, const std::vector<std::uint8_t>& _expected, const wchar_t* _what)
{
  Assert::AreEqual(_expected.size(), _written.size(), _what);
  for (std::size_t index = 0; index < _expected.size(); ++index)
  {
    if (std::to_integer<std::uint8_t>(_written[index]) != _expected[index])
    {
      Assert::Fail(_what);
    }
  }
}

/// Reads the record back and compares it with the one written. Also the truncation case, which is
/// the same assertion for every record: every prefix shorter than the whole must be refused.
template <class T> void RoundTripAndTruncations(const T& _record, const wchar_t* _what)
{
  const std::vector<std::byte> written = Bytes(_record);
  Neuron::ByteReader reader(written);
  T read{};
  Assert::IsTrue(Frontier::Read(reader, read), _what);
  Assert::IsTrue(read == _record, _what);

  for (std::size_t length = 0; length < written.size(); ++length)
  {
    Neuron::ByteReader shortReader(std::span<const std::byte>(written.data(), length));
    T refused{};
    Assert::IsFalse(Frontier::Read(shortReader, refused), _what);
  }
}

Frontier::DeviceState ADevice()
{
  Frontier::DeviceState record{};
  record.id = 0x01020304;
  record.design = 7;
  record.seat = 3;
  record.x = 1000;
  record.y = -40;
  record.z = 65536;
  record.heading = 0x80;
  record.hitPoints = 1234;
  record.rank = 5;
  record.target = 0x0A0B0C0D;
  record.targetKind = Frontier::ObjectKind::Structure;
  record.stances = {Frontier::PrimaryOrder::AttackMove, Frontier::FireStance::ReturnFire, Frontier::RangeStance::LongRange,
                    Frontier::RetreatStance::AtQuarter, Frontier::MovementStance::HoldPosition};
  return record;
}

Frontier::DeviceChange AChange()
{
  Frontier::DeviceChange record{};
  record.id = 0x11223344;
  record.mask = static_cast<std::uint8_t>(Frontier::DeviceField::Position) | static_cast<std::uint8_t>(Frontier::DeviceField::Heading) |
                static_cast<std::uint8_t>(Frontier::DeviceField::HitPoints) | static_cast<std::uint8_t>(Frontier::DeviceField::Stances);
  record.deltaX = -3;
  record.deltaY = 0;
  record.deltaZ = 7;
  record.heading = 0x40;
  record.hitPoints = 900;
  record.stances = {Frontier::PrimaryOrder::AttackMove, Frontier::FireStance::ReturnFire, Frontier::RangeStance::LongRange,
                    Frontier::RetreatStance::AtQuarter, Frontier::MovementStance::HoldPosition};
  return record;
}

Frontier::StructureState AStructure()
{
  Frontier::StructureState record{};
  record.id = 9;
  record.design = 2;
  record.seat = 1;
  record.cellX = 40;
  record.cellY = 41;
  record.y = -256;
  record.phase = Frontier::StructurePhase::Standing;
  record.hitPoints = 760;
  record.buildPercent = 100;
  record.moduleCount = 2;
  record.modules = {3, 4, 0, 0};
  return record;
}

Frontier::WreckState AWreck()
{
  Frontier::WreckState record{};
  record.id = 0x20;
  record.design = 5;
  record.seat = 2;
  record.origin = static_cast<std::uint8_t>(Frontier::ObjectKind::Device);
  record.x = 10;
  record.y = -1;
  record.z = 20;
  record.heading = 0xC0;
  return record;
}

Frontier::FeatureState AFeature()
{
  Frontier::FeatureState record{};
  record.id = 0x31;
  record.design = 6;
  record.cellX = 12;
  record.cellY = 13;
  record.y = 64;
  record.heading = 0x10;
  return record;
}

Frontier::SeatState ASeat()
{
  Frontier::SeatState record{};
  record.seat = 1;
  record.powerHundredths = 40000;
  record.stockpileCapHundredths = 100000;
  record.extractedHundredths = 123456789;
  record.researchItem = Frontier::NO_RESEARCH_ITEM;
  record.researchRemainingTicks = 0;
  record.victory = 0;
  record.deviceCount = 12;
  record.deviceCap = 200;
  record.structureCount = 3;
  record.structureCap = 300;
  return record;
}

Frontier::DesignState ADesign()
{
  Frontier::DesignState record{};
  record.seat = 2;
  record.index = 1;
  record.chassis = 0;
  record.drive = 1;
  record.moduleCount = 2;
  record.modules = {3, 4, 0, 0, 0, 0, 0, 0};
  return record;
}

Frontier::Event AnEvent()
{
  Frontier::Event record{};
  record.kind = Frontier::EventKind::Destroyed;
  record.source = 0x55;
  record.sourceKind = Frontier::ObjectKind::Device;
  record.target = 0x66;
  record.targetKind = Frontier::ObjectKind::Structure;
  record.x = 7;
  record.y = -8;
  record.z = 9;
  record.tick = 4242;
  return record;
}

Frontier::FogDelta AFogRun()
{
  Frontier::FogDelta record{};
  record.firstCell = 1024;
  record.cells = 48;
  record.state = Frontier::FogState::Visible;
  return record;
}

Frontier::MatchSettings TheLobby()
{
  Frontier::MatchSettings settings{};
  settings.seed = 0x0102030405060708u;
  settings.sizeClass = Frontier::SizeClass::Small;
  settings.seatCount = 3;
  settings.baseLevel = Frontier::BaseLevel::Small;
  settings.powerLevel = Frontier::PowerLevel::Medium;
  settings.technologyTiers = 1;
  settings.victory = Frontier::VictoryCondition::Annihilation;
  settings.survivalTicks = 36000;
  settings.deviceCapLevel = Frontier::DeviceCapLevel::Medium;
  settings.seats[0] = {Frontier::SeatKind::Human, 0, false};
  settings.seats[1] = {Frontier::SeatKind::Ai, 1, true};
  settings.seats[2] = {Frontier::SeatKind::Empty, 2, false};
  return settings;
}

Frontier::LandscapeDefinition TheGround()
{
  Frontier::LandscapeDefinition definition{};
  definition.version = Frontier::LANDSCAPE_DEFINITION_VERSION;
  definition.sizeClass = Frontier::SizeClass::Small;
  definition.cellsPerSide = Frontier::SIZE_CLASS_CELLS[0];
  definition.seed = 99;
  definition.palette = "Default";
  definition.tiles = {{0, 0, 512, 170, 90, 100, 48, 70, 1, 32, "Desert"}};
  definition.starts = {{16, 16}, {100, 100}};
  definition.deposits = {{20, 20}, {30, 30}, {40, 40}};
  return definition;
}

} // namespace

TEST_CLASS(RecordsTests)
{
public:
  TEST_METHOD(EveryRecordIsExactlyTheseBytes)
  {
    // The pin. These are written out by hand from §5.3's layout, not recorded from a run: a test
    // that records what the code did asserts nothing about what the protocol IS.
    AssertBytes(Bytes(ADevice()), {0x04, 0x03, 0x02, 0x01, 0x07, 0x00, 0x00, 0x00, 0x03, 0xE8, 0x03, 0x00, 0x00, 0xD8, 0xFF, 0xFF,
                                   0xFF, 0x00, 0x00, 0x01, 0x00, 0x80, 0xD2, 0x04, 0x05, 0x0D, 0x0C, 0x0B, 0x0A, 0x01, 0x5D},
                L"DeviceState");
    AssertBytes(Bytes(AChange()), {0x44, 0x33, 0x22, 0x11, 0x0F, 0xFD, 0xFF, 0x00, 0x00, 0x07, 0x00, 0x40, 0x84, 0x03, 0x5D},
                L"DeviceChange");
    AssertBytes(Bytes(AStructure()), {0x09, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x28, 0x00, 0x29, 0x00,
                                      0x00, 0xFF, 0xFF, 0xFF, 0x02, 0xF8, 0x02, 0x64, 0x02, 0x03, 0x04, 0x00, 0x00},
                L"StructureState");
    AssertBytes(Bytes(AWreck()), {0x20, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0A, 0x00,
                                  0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x14, 0x00, 0x00, 0x00, 0xC0},
                L"WreckState");
    AssertBytes(Bytes(AFeature()), {0x31, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x0D, 0x00, 0x40, 0x00, 0x00, 0x00, 0x10},
                L"FeatureState");
    AssertBytes(Bytes(ASeat()), {0x01, 0x40, 0x9C, 0x00, 0x00, 0xA0, 0x86, 0x01, 0x00, 0x15, 0xCD, 0x5B, 0x07, 0x00, 0x00, 0x00, 0x00,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0xC8, 0x00, 0x03, 0x00, 0x2C, 0x01},
                L"SeatState");
    AssertBytes(Bytes(ADesign()), {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00,
                                   0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                L"DesignState");
    AssertBytes(Bytes(AnEvent()), {0x02, 0x55, 0x00, 0x00, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00,
                                   0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0x09, 0x00, 0x00, 0x00, 0x92, 0x10, 0x00, 0x00},
                L"Event");
    AssertBytes(Bytes(AFogRun()), {0x00, 0x04, 0x00, 0x00, 0x30, 0x00, 0x02}, L"FogDelta");
  }

  TEST_METHOD(TheRecordSizesAreWhatTheHeaderSays)
  {
    Assert::AreEqual(Frontier::DEVICE_STATE_BYTES, Bytes(ADevice()).size());
    Assert::AreEqual(Frontier::STRUCTURE_STATE_BYTES, Bytes(AStructure()).size());
    Assert::AreEqual(Frontier::WRECK_STATE_BYTES, Bytes(AWreck()).size());
    Assert::AreEqual(Frontier::FEATURE_STATE_BYTES, Bytes(AFeature()).size());
    Assert::AreEqual(Frontier::SEAT_STATE_BYTES, Bytes(ASeat()).size());
    Assert::AreEqual(Frontier::DESIGN_STATE_BYTES, Bytes(ADesign()).size());
    Assert::AreEqual(Frontier::EVENT_BYTES, Bytes(AnEvent()).size());
    Assert::AreEqual(Frontier::FOG_DELTA_BYTES, Bytes(AFogRun()).size());
  }

  TEST_METHOD(ADeviceChangeIsFourteenBytesPlusItsMask)
  {
    // TechnicalDesign.md §5.7's arithmetic, which the whole bandwidth estimate rests on. The mask
    // has every bit set here, so this is the worst case rather than a typical one.
    Assert::AreEqual(Frontier::DEVICE_CHANGE_MAX_BYTES + Frontier::DEVICE_CHANGE_MASK_BYTES, Bytes(AChange()).size());

    // And a device that only moved costs the id, the mask and six bytes.
    Frontier::DeviceChange moved = AChange();
    moved.mask = static_cast<std::uint8_t>(Frontier::DeviceField::Position);
    Assert::AreEqual(static_cast<std::size_t>(11), Bytes(moved).size());

    // A device that did nothing is never sent, but if it were it would be five bytes.
    Frontier::DeviceChange still = AChange();
    still.mask = 0;
    Assert::AreEqual(static_cast<std::size_t>(5), Bytes(still).size());
  }

  TEST_METHOD(EveryRecordSurvivesARoundTripAndRefusesEveryTruncation)
  {
    RoundTripAndTruncations(ADevice(), L"DeviceState");
    RoundTripAndTruncations(AChange(), L"DeviceChange");
    RoundTripAndTruncations(AStructure(), L"StructureState");
    RoundTripAndTruncations(AWreck(), L"WreckState");
    RoundTripAndTruncations(AFeature(), L"FeatureState");
    RoundTripAndTruncations(ASeat(), L"SeatState");
    RoundTripAndTruncations(ADesign(), L"DesignState");
    RoundTripAndTruncations(AnEvent(), L"Event");
    RoundTripAndTruncations(AFogRun(), L"FogDelta");
  }

  TEST_METHOD(AMaskedChangeCarriesOnlyTheFieldsItsMaskNames)
  {
    for (std::uint8_t mask = 0; mask < 16; ++mask)
    {
      Frontier::DeviceChange written = AChange();
      written.mask = mask;
      const std::vector<std::byte> bytes = Bytes(written);
      Neuron::ByteReader reader(bytes);
      Frontier::DeviceChange read{};
      Assert::IsTrue(Frontier::Read(reader, read));
      // A field the mask leaves out reads back as zero rather than as what was in the record, which
      // is what tells the decoder to keep the baseline's value.
      Assert::AreEqual(static_cast<int>(mask), static_cast<int>(read.mask));
      if ((mask & static_cast<std::uint8_t>(Frontier::DeviceField::Position)) == 0)
      {
        Assert::AreEqual(0, static_cast<int>(read.deltaX));
      }
      if ((mask & static_cast<std::uint8_t>(Frontier::DeviceField::HitPoints)) == 0)
      {
        Assert::AreEqual(0, static_cast<int>(read.hitPoints));
      }
    }
  }

  TEST_METHOD(AMaskBitThisVersionDoesNotKnowIsRefused)
  {
    // A frame from a newer protocol: the fields after the unknown bit cannot be found, so reading
    // on would read whatever follows as a heading. Refused instead.
    Frontier::DeviceChange written = AChange();
    written.mask = 0x10;
    Neuron::ByteWriter writer;
    writer.Write(written.id);
    writer.Write(written.mask);
    Neuron::ByteReader reader(writer.Bytes());
    Frontier::DeviceChange read{};
    Assert::IsFalse(Frontier::Read(reader, read));
  }

  TEST_METHOD(TheOrderAndTheFourStancesFitOneByteAndTheGapIsRefused)
  {
    // 7 orders x 3 fire x 2 range x 3 retreat x 2 movement is 252 of the 256 values a byte has.
    // Every one of them must round-trip, and the four left over must be refused: they are what a
    // hostile byte would name an order the enumeration does not have.
    std::vector<bool> seen(256, false);
    for (std::uint8_t order = 0; order < Frontier::PRIMARY_ORDER_COUNT; ++order)
    {
      for (std::uint8_t fire = 0; fire < 3; ++fire)
      {
        for (std::uint8_t range = 0; range < 2; ++range)
        {
          for (std::uint8_t retreat = 0; retreat < 3; ++retreat)
          {
            for (std::uint8_t movement = 0; movement < 2; ++movement)
            {
              const Frontier::OrderAndStances written{static_cast<Frontier::PrimaryOrder>(order), static_cast<Frontier::FireStance>(fire),
                                                      static_cast<Frontier::RangeStance>(range),
                                                      static_cast<Frontier::RetreatStance>(retreat),
                                                      static_cast<Frontier::MovementStance>(movement)};
              const std::uint8_t packed = Frontier::PackOrderAndStances(written);
              Assert::IsFalse(seen[packed], L"two combinations must never pack to one byte");
              seen[packed] = true;
              Frontier::OrderAndStances read{};
              Assert::IsTrue(Frontier::UnpackOrderAndStances(packed, read));
              Assert::IsTrue(read == written);
            }
          }
        }
      }
    }
    std::size_t used = 0;
    for (std::size_t value = 0; value < seen.size(); ++value)
    {
      used += seen[value] ? 1 : 0;
      if (!seen[value])
      {
        Frontier::OrderAndStances refused{};
        Assert::IsFalse(Frontier::UnpackOrderAndStances(static_cast<std::uint8_t>(value), refused));
      }
    }
    Assert::AreEqual(static_cast<std::size_t>(252), used);
  }

  TEST_METHOD(APositionIsQuantizedToAQuarterOfAWorldUnitAndBackToTheMiddleOfIt)
  {
    Assert::AreEqual(64, Frontier::SUBUNITS_PER_WIRE_UNIT, L"a quarter of 256 subunits");
    // The round trip is never worse than half a step, on both sides of zero: floor division is
    // what makes the step uniform across it, where truncation would make the step at zero double.
    for (std::int32_t subunits = -4096; subunits <= 4096; ++subunits)
    {
      const std::int32_t back = Frontier::SubunitsFromWire(Frontier::WireFromSubunits(subunits));
      const std::int32_t error = back > subunits ? back - subunits : subunits - back;
      Assert::IsTrue(error <= Frontier::SUBUNITS_PER_WIRE_UNIT / 2);
    }
    Assert::AreEqual(-1, Frontier::WireFromSubunits(-1), L"one subunit below zero is the quarter below zero");
    Assert::AreEqual(0, Frontier::WireFromSubunits(1));
  }

  TEST_METHOD(AHeadingIsTheHighByteOfItsBinaryAngle)
  {
    Assert::AreEqual(0, static_cast<int>(Frontier::WireFromFacing(0)));
    Assert::AreEqual(64, static_cast<int>(Frontier::WireFromFacing(0x4000)), L"a quarter turn");
    Assert::AreEqual(0x4000, static_cast<int>(Frontier::FacingFromWire(64)));
    for (std::uint32_t facing = 0; facing < 65536; facing += 256)
    {
      Assert::AreEqual(static_cast<int>(facing),
                       static_cast<int>(Frontier::FacingFromWire(Frontier::WireFromFacing(static_cast<std::uint16_t>(facing)))));
    }
  }

  TEST_METHOD(AValueNoEnumerationHasIsRefused)
  {
    // The whole reason every enumeration reads through a bound: a byte from the wire must never
    // become a StructurePhase, an ObjectKind or a FogState the tree has no case for.
    Neuron::ByteWriter writer;
    Frontier::StructureState written = AStructure();
    Frontier::Write(writer, written);
    std::vector<std::byte> bytes(writer.Bytes().begin(), writer.Bytes().end());
    bytes[17] = static_cast<std::byte>(Frontier::STRUCTURE_PHASE_COUNT); // the phase's byte
    Neuron::ByteReader reader(bytes);
    Frontier::StructureState read{};
    Assert::IsFalse(Frontier::Read(reader, read), L"a fifth structure phase");

    Frontier::FogDelta empty = AFogRun();
    empty.cells = 0;
    const std::vector<std::byte> emptyRun = Bytes(empty);
    Neuron::ByteReader emptyReader(emptyRun);
    Frontier::FogDelta readRun{};
    Assert::IsFalse(Frontier::Read(emptyReader, readRun), L"a run of no cells");
  }
};

TEST_CLASS(MessagesTests)
{
public:
  TEST_METHOD(EveryMessageSurvivesARoundTripBehindItsKind)
  {
    const auto trip = [](const auto& _message, Frontier::MessageKind _kind, const wchar_t* _what)
    {
      Neuron::ByteWriter writer;
      Frontier::Write(writer, _message);
      Neuron::ByteReader reader(writer.Bytes());
      Frontier::MessageKind kind{};
      Assert::IsTrue(Frontier::ReadMessageKind(reader, kind), _what);
      Assert::AreEqual(static_cast<int>(_kind), static_cast<int>(kind), _what);
      std::remove_cvref_t<decltype(_message)> read{};
      Assert::IsTrue(Frontier::Read(reader, read), _what);
      Assert::IsTrue(read == _message, _what);
    };

    Frontier::Join join{};
    join.protocolVersion = Frontier::NET_PROTOCOL_VERSION;
    join.contentHash = 0xDEADBEEFCAFEBABEu;
    join.token = 0x1122334455667788u;
    join.nameBytes = 4;
    join.name = {};
    join.name[0] = 'Z';
    join.name[1] = 'w';
    join.name[2] = 'a';
    join.name[3] = 'l';
    trip(join, Frontier::MessageKind::Join, L"Join");

    Frontier::JoinAccepted accepted{};
    accepted.seat = 1;
    accepted.tick = 4242;
    accepted.settings = TheLobby();
    accepted.landscape = TheGround();
    trip(accepted, Frontier::MessageKind::JoinAccepted, L"JoinAccepted");

    Frontier::JoinRefused refused{};
    refused.reason = Frontier::RefusalReason::ContentHash;
    refused.hostProtocolVersion = Frontier::NET_PROTOCOL_VERSION;
    refused.hostContentHash = 7;
    trip(refused, Frontier::MessageKind::JoinRefused, L"JoinRefused");

    Frontier::Frame frame{};
    frame.sequence = 12;
    frame.baselineSequence = 9;
    frame.tick = 24;
    frame.createdDevices = {ADevice()};
    frame.createdStructures = {AStructure()};
    frame.createdWrecks = {AWreck()};
    frame.createdFeatures = {AFeature()};
    frame.designs = {ADesign()};
    frame.changedDevices = {AChange()};
    frame.changedStructures = {AStructure()};
    frame.removed = {3, 4, 5};
    frame.events = {AnEvent()};
    frame.fog = {AFogRun()};
    frame.seat = ASeat();
    trip(frame, Frontier::MessageKind::Frame, L"Frame");

    Frontier::Fragment fragment{};
    fragment.frameSequence = 12;
    fragment.index = 1;
    fragment.count = 3;
    fragment.bytes = {std::byte{1}, std::byte{2}, std::byte{3}};
    trip(fragment, Frontier::MessageKind::Fragment, L"Fragment");

    Frontier::Ack ack{};
    ack.frameSequence = 11;
    ack.orderSequence = 40;
    trip(ack, Frontier::MessageKind::Ack, L"Ack");

    Frontier::Orders orders{};
    orders.ack = ack;
    Frontier::Order move{};
    move.tick = 100;
    move.seat = 1;
    move.kind = Frontier::OrderKind::Move;
    move.operands = {7, 1000, 2000, 0};
    orders.orders = {{41, move}};
    trip(orders, Frontier::MessageKind::Orders, L"Orders");

    Frontier::Heartbeat heartbeat{};
    heartbeat.ack = ack;
    heartbeat.tick = 26;
    trip(heartbeat, Frontier::MessageKind::Heartbeat, L"Heartbeat");
  }

  TEST_METHOD(AFrameThatClaimsMoreRecordsThanItCarriesIsRefused)
  {
    // The bound is the reader's and not the stream's: a count a hostile datagram cannot back up
    // must fail on the first record it cannot read, not after a reserve of four billion.
    Neuron::ByteWriter writer;
    writer.Write(static_cast<std::uint32_t>(12));         // sequence
    writer.Write(static_cast<std::uint32_t>(0));          // baseline
    writer.Write(static_cast<std::uint32_t>(24));         // tick
    writer.Write(static_cast<std::uint32_t>(0xFFFFFFFF)); // created devices: more than any match holds
    Neuron::ByteReader reader(writer.Bytes());
    Frontier::Frame frame{};
    Assert::IsFalse(Frontier::Read(reader, frame));

    // And a count inside the bound that the datagram does not carry fails too.
    Neuron::ByteWriter shortWriter;
    shortWriter.Write(static_cast<std::uint32_t>(12));
    shortWriter.Write(static_cast<std::uint32_t>(0));
    shortWriter.Write(static_cast<std::uint32_t>(24));
    shortWriter.Write(static_cast<std::uint32_t>(2));
    Neuron::ByteReader shortReader(shortWriter.Bytes());
    Frontier::Frame shortFrame{};
    Assert::IsFalse(Frontier::Read(shortReader, shortFrame));
  }

  TEST_METHOD(AFragmentOutsideItsOwnCountIsRefused)
  {
    const auto refuse = [](std::uint8_t _index, std::uint8_t _count, const wchar_t* _what)
    {
      Neuron::ByteWriter writer;
      writer.Write(static_cast<std::uint32_t>(1));
      writer.Write(_index);
      writer.Write(_count);
      writer.WriteSpan(std::span<const std::byte>{});
      Neuron::ByteReader reader(writer.Bytes());
      Frontier::Fragment fragment{};
      Assert::IsFalse(Frontier::Read(reader, fragment), _what);
    };
    refuse(0, 0, L"a frame with no pieces");
    refuse(3, 3, L"the fourth piece of three");
    refuse(0, static_cast<std::uint8_t>(Frontier::MAX_FRAGMENTS + 1), L"more pieces than a frame may have");
  }

  TEST_METHOD(AMessageKindThisVersionDoesNotHaveIsRefused)
  {
    Neuron::ByteWriter writer;
    writer.Write(Frontier::MESSAGE_KIND_COUNT);
    Neuron::ByteReader reader(writer.Bytes());
    Frontier::MessageKind kind{};
    Assert::IsFalse(Frontier::ReadMessageKind(reader, kind));
  }
};

} // namespace NetTests
