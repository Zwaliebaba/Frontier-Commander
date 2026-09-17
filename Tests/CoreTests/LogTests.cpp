#include "pch.h"

#include "Log.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

std::string ReadWhole(const std::filesystem::path& _file)
{
  std::ifstream in(_file, std::ios::in | std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

struct ScratchLog
{
  std::filesystem::path directory;
  std::filesystem::path file;

  ScratchLog()
  {
    directory = std::filesystem::temp_directory_path() / L"FrontierCommanderTests" /
                std::filesystem::path(std::to_wstring(::GetCurrentProcessId()) + L"-log");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    file = directory / L"Logs" / L"Test.log";
  }

  ~ScratchLog()
  {
    Neuron::Log::Close();
    Neuron::Log::SetTick(Neuron::Log::NO_TICK);
    Neuron::Log::SetMinimumLevel(Neuron::LogLevel::Info);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
};

} // namespace

TEST_CLASS(LogTests)
{
public:
  TEST_METHOD(LinesCarryTheLevelAndTheTickOrTheTime)
  {
    ScratchLog scratch;
    Assert::IsTrue(Neuron::Log::Open(scratch.file), L"Open failed; the directory should have been created");
    Assert::IsTrue(Neuron::Log::IsOpen());
    Neuron::Log::SetTick(42);
    Neuron::Log::Write(Neuron::LogLevel::Info, "hello");
    Neuron::Log::SetTick(Neuron::Log::NO_TICK);
    Neuron::Log::Write(Neuron::LogLevel::Warning, "later");
    Neuron::Log::Close();
    const std::string text = ReadWhole(scratch.file);
    Assert::IsTrue(text.find("[tick 42] [info] hello\n") != std::string::npos, L"the tick line is missing");
    Assert::IsTrue(text.find("] [warning] later\n") != std::string::npos, L"the warning line is missing");
    Assert::IsTrue(text.find("[tick 42] [warning]") == std::string::npos, L"a line without a tick carried one");
  }

  TEST_METHOD(LinesBelowTheMinimumAreDropped)
  {
    ScratchLog scratch;
    Assert::IsTrue(Neuron::Log::Open(scratch.file));
    Neuron::Log::Write(Neuron::LogLevel::Debug, "quiet");
    Neuron::Log::SetMinimumLevel(Neuron::LogLevel::Debug);
    Neuron::Log::Write(Neuron::LogLevel::Debug, "loud");
    Neuron::Log::Close();
    const std::string text = ReadWhole(scratch.file);
    Assert::IsTrue(text.find("quiet") == std::string::npos);
    Assert::IsTrue(text.find("[debug] loud") != std::string::npos);
  }

  TEST_METHOD(AFullLogRotatesOnOpen)
  {
    ScratchLog scratch;
    Assert::IsTrue(Neuron::Log::Open(scratch.file, 64));
    for (int line = 0; line < 20; ++line)
    {
      Neuron::Log::Write(Neuron::LogLevel::Info, "a line long enough to fill the small limit quickly");
    }
    Assert::IsTrue(Neuron::Log::BytesWritten() > 64);
    Neuron::Log::Close();
    Assert::IsTrue(Neuron::Log::Open(scratch.file, 64));
    Neuron::Log::Write(Neuron::LogLevel::Info, "fresh");
    Neuron::Log::Close();
    std::filesystem::path rotated = scratch.file;
    rotated += L".1";
    Assert::IsTrue(std::filesystem::exists(rotated), L"the full log was not rotated to .1");
    Assert::IsTrue(std::filesystem::file_size(rotated) > 64);
    const std::string fresh = ReadWhole(scratch.file);
    Assert::IsTrue(fresh.find("fresh") != std::string::npos);
    Assert::IsTrue(fresh.find("quickly") == std::string::npos, L"the new log still holds the old lines");
  }

  TEST_METHOD(WritingBeforeOpenIsHarmless)
  {
    Neuron::Log::Close();
    Neuron::Log::Write(Neuron::LogLevel::Error, "nowhere to go");
    Assert::IsFalse(Neuron::Log::IsOpen());
  }
};

} // namespace CoreTests
