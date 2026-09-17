#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace StaleTests
{

TEST_CLASS(StaleTests)
{
public:
  TEST_METHOD(ARealTest)
  {
    Assert::AreEqual(1, 1);
  }
};

} // namespace StaleTests
