#include "pch.h"

#include "Assert.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

// vstest reports an empty suite as a pass, so every suite carries this placeholder until its
// first real test replaces it (AGENTS.md §3). It links the library under test so that a suite
// whose library does not build cannot pass either.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(TheSuiteRunsAndTheLibraryLinks)
  {
    int evaluated = 0;
    FRONTIER_VERIFY(++evaluated == 1);
    Assert::AreEqual(1, evaluated);
  }
};

} // namespace CoreTests
