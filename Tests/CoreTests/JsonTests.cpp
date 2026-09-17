#include "pch.h"

#include "Json.h"

#include "JsonFixtures.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace CoreTests
{

namespace
{

bool Equal(const Neuron::JsonValue& _a, const Neuron::JsonValue& _b)
{
  if (_a.Kind() != _b.Kind())
  {
    return false;
  }
  switch (_a.Kind())
  {
  case Neuron::JsonKind::Null:
    return true;
  case Neuron::JsonKind::Bool:
    return _a.AsBool() == _b.AsBool();
  case Neuron::JsonKind::Integer:
    return _a.AsInteger() == _b.AsInteger();
  case Neuron::JsonKind::Double:
    return _a.AsDouble() == _b.AsDouble();
  case Neuron::JsonKind::String:
    return _a.AsString() == _b.AsString();
  case Neuron::JsonKind::Array:
    if (_a.Size() != _b.Size())
    {
      return false;
    }
    for (std::size_t index = 0; index < _a.Size(); ++index)
    {
      if (!Equal(_a.At(index), _b.At(index)))
      {
        return false;
      }
    }
    return true;
  case Neuron::JsonKind::Object:
    if (_a.Size() != _b.Size())
    {
      return false;
    }
    for (std::size_t index = 0; index < _a.Size(); ++index)
    {
      if (_a.KeyAt(index) != _b.KeyAt(index) || !Equal(_a.ValueAt(index), _b.ValueAt(index)))
      {
        return false;
      }
    }
    return true;
  }
  return false;
}

std::wstring Wide(const char* _text)
{
  std::wstring wide;
  for (const char* c = _text; *c != '\0'; ++c)
  {
    wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*c)));
  }
  return wide;
}

Neuron::JsonValue ParseOrFail(std::string_view _text)
{
  Neuron::JsonValue value;
  Neuron::JsonError error;
  Assert::IsTrue(Neuron::ParseJson(_text, "test.json", value, error), Wide(error.ToString().c_str()).c_str());
  return value;
}

} // namespace

TEST_CLASS(JsonTests)
{
public:
  TEST_METHOD(EveryAcceptedDocumentParsesAndRoundTrips)
  {
    for (const AcceptedJson& fixture : ACCEPTED_JSON)
    {
      Neuron::JsonValue value;
      Neuron::JsonError error;
      const bool parsed = Neuron::ParseJson(fixture.text, fixture.name, value, error);
      Assert::IsTrue(parsed, Wide((std::string(fixture.name) + ": " + error.ToString()).c_str()).c_str());
      for (const int indent : {0, 2})
      {
        const std::string written = Neuron::WriteJson(value, indent);
        Neuron::JsonValue again;
        Assert::IsTrue(Neuron::ParseJson(written, fixture.name, again, error),
                       Wide((std::string(fixture.name) + " (rewritten): " + error.ToString()).c_str()).c_str());
        Assert::IsTrue(Equal(value, again), Wide((std::string(fixture.name) + ": the rewritten document differs").c_str()).c_str());
      }
    }
  }

  TEST_METHOD(EveryRejectedDocumentFailsWhereExpected)
  {
    for (const RejectedJson& fixture : REJECTED_JSON)
    {
      Neuron::JsonValue value;
      Neuron::JsonError error;
      const bool parsed = Neuron::ParseJson(fixture.text, fixture.name, value, error);
      Assert::IsFalse(parsed, Wide((std::string(fixture.name) + ": parsed").c_str()).c_str());
      Assert::AreEqual(fixture.line, error.line, Wide((std::string(fixture.name) + ": line; " + error.ToString()).c_str()).c_str());
      Assert::AreEqual(fixture.column, error.column, Wide((std::string(fixture.name) + ": column; " + error.ToString()).c_str()).c_str());
      Assert::AreEqual(std::string(fixture.name), error.file);
    }
  }

  TEST_METHOD(NumbersAreIntegersWhenIntegral)
  {
    Assert::IsTrue(ParseOrFail("1").IsInteger());
    Assert::AreEqual(std::int64_t{1}, ParseOrFail("1").AsInteger());
    Assert::IsTrue(ParseOrFail("-0").IsInteger());
    Assert::AreEqual(std::int64_t{0}, ParseOrFail("-0").AsInteger());
    Assert::IsTrue(ParseOrFail("1.0").IsDouble());
    Assert::IsTrue(ParseOrFail("1e2").IsDouble());
    Assert::AreEqual(100.0, ParseOrFail("1e2").AsDouble());
    Assert::AreEqual(std::int64_t{9223372036854775807}, ParseOrFail("9223372036854775807").AsInteger());
    Assert::IsTrue(ParseOrFail("9223372036854775808").IsDouble());
    Assert::AreEqual(2.0, ParseOrFail("2").AsDouble());
  }

  TEST_METHOD(StringsDecodeEscapesToUtf8)
  {
    Assert::AreEqual(std::string("\" \\ / \b \f \n \r \t"), ParseOrFail("\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"").AsString());
    Assert::AreEqual(std::string("A"), ParseOrFail("\"\\u0041\"").AsString());
    Assert::AreEqual(std::string("\xC3\xA9"), ParseOrFail("\"\\u00e9\"").AsString());
    Assert::AreEqual(std::string("\xE2\x82\xAC"), ParseOrFail("\"\\u20ac\"").AsString());
    Assert::AreEqual(std::string("\xF0\x9F\x98\x80"), ParseOrFail("\"\\ud83d\\ude00\"").AsString());
  }

  TEST_METHOD(ObjectsKeepInsertionOrderAndFindMembers)
  {
    const Neuron::JsonValue object = ParseOrFail("{\"z\": 1, \"a\": [1, 2], \"m\": {\"k\": true}}");
    Assert::AreEqual(std::size_t{3}, object.Size());
    Assert::AreEqual(std::string("z"), object.KeyAt(0));
    Assert::AreEqual(std::string("a"), object.KeyAt(1));
    Assert::AreEqual(std::string("m"), object.KeyAt(2));
    Assert::IsNotNull(object.Find("a"));
    Assert::AreEqual(std::size_t{2}, object.Find("a")->Size());
    Assert::AreEqual(std::int64_t{2}, object.Find("a")->At(1).AsInteger());
    Assert::IsTrue(object.Find("m")->Find("k")->AsBool());
    Assert::IsNull(object.Find("missing"));
  }

  TEST_METHOD(TheWriterEmitsWhatTheReaderAcceptsInAStableOrder)
  {
    Neuron::JsonValue object = Neuron::JsonValue::Object();
    object.Set("name", Neuron::JsonValue::String("tab\there \"quoted\""));
    object.Set("count", Neuron::JsonValue::Integer(-7));
    object.Set("ratio", Neuron::JsonValue::Double(0.5));
    Neuron::JsonValue& list = object.Set("list", Neuron::JsonValue::Array());
    list.Push(Neuron::JsonValue::Bool(true));
    list.Push(Neuron::JsonValue::Null());
    object.Set("count", Neuron::JsonValue::Integer(8)); // replaces in place, keeping the position
    Assert::AreEqual(std::string("{\"name\":\"tab\\there \\\"quoted\\\"\",\"count\":8,\"ratio\":0.5,\"list\":[true,null]}"),
                     Neuron::WriteJson(object, 0));
    Assert::AreEqual(
      std::string(
        "{\n  \"name\": \"tab\\there \\\"quoted\\\"\",\n  \"count\": 8,\n  \"ratio\": 0.5,\n  \"list\": [\n    true,\n    null\n  ]\n}"),
      Neuron::WriteJson(object, 2));
    Assert::AreEqual(std::string("[]"), Neuron::WriteJson(Neuron::JsonValue::Array(), 2));
    Assert::AreEqual(std::string("{}"), Neuron::WriteJson(Neuron::JsonValue::Object(), 2));
    Assert::AreEqual(std::string("\"\\u0001\""), Neuron::WriteJson(Neuron::JsonValue::String(std::string(1, '\x01')), 0));
  }

  TEST_METHOD(TheErrorNamesTheFileLineAndColumn)
  {
    Neuron::JsonValue value;
    Neuron::JsonError error;
    Assert::IsFalse(Neuron::ParseJson("{\n  \"a\": tru\n}", "Content/Components.json", value, error));
    Assert::AreEqual(std::string("Content/Components.json"), error.file);
    Assert::AreEqual(2, error.line);
    Assert::AreEqual(8, error.column);
    Assert::AreEqual(std::string("Content/Components.json(2,8): expected a value"), error.ToString());
  }
};

} // namespace CoreTests
