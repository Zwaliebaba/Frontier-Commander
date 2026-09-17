#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ContentTests
{

TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(TheSuiteRuns)
  {
    Assert::IsTrue(true);
  }
};

} // namespace ContentTests
