#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ContentTests
{

TEST_CLASS(ContentTests)
{
public:
  TEST_METHOD(ARealTest)
  {
    Assert::AreEqual(1, 1);
  }
};

} // namespace ContentTests
