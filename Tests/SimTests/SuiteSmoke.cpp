#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace SimTests
{

// vstest reports an empty suite as a pass, so every suite carries this placeholder until its
// first real test replaces it (AGENTS.md §3).
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(TheSuiteRuns)
  {
    int evaluated = 0;
    FRONTIER_VERIFY(++evaluated == 1);
    Assert::AreEqual(1, evaluated);
  }
};

} // namespace SimTests
