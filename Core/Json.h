#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// JSON, read and written by code in this tree (AGENTS.md R14; TechnicalDesign.md §8): a strict
// reader of RFC 8259 and nothing more, with the file, line and column in every error, and a
// writer that emits what the reader accepts with a stable member order. A document is accepted
// whole or rejected whole. The tree is standard containers (R15); an object keeps its members in
// insertion order, which is the order the writer emits.

namespace Neuron
{

enum class JsonKind : std::uint8_t
{
  Null,
  Bool,
  Integer,
  Double,
  String,
  Array,
  Object
};

class JsonValue
{
public:
  JsonValue() noexcept = default;

  [[nodiscard]] static JsonValue Null() noexcept
  {
    return JsonValue();
  }
  [[nodiscard]] static JsonValue Bool(bool _value) noexcept;
  [[nodiscard]] static JsonValue Integer(std::int64_t _value) noexcept;
  [[nodiscard]] static JsonValue Double(double _value) noexcept;
  [[nodiscard]] static JsonValue String(std::string _value);
  [[nodiscard]] static JsonValue Array();
  [[nodiscard]] static JsonValue Object();

  [[nodiscard]] JsonKind Kind() const noexcept
  {
    return m_kind;
  }
  [[nodiscard]] bool IsNull() const noexcept
  {
    return m_kind == JsonKind::Null;
  }
  [[nodiscard]] bool IsBool() const noexcept
  {
    return m_kind == JsonKind::Bool;
  }
  [[nodiscard]] bool IsInteger() const noexcept
  {
    return m_kind == JsonKind::Integer;
  }
  [[nodiscard]] bool IsDouble() const noexcept
  {
    return m_kind == JsonKind::Double;
  }
  [[nodiscard]] bool IsNumber() const noexcept
  {
    return IsInteger() || IsDouble();
  }
  [[nodiscard]] bool IsString() const noexcept
  {
    return m_kind == JsonKind::String;
  }
  [[nodiscard]] bool IsArray() const noexcept
  {
    return m_kind == JsonKind::Array;
  }
  [[nodiscard]] bool IsObject() const noexcept
  {
    return m_kind == JsonKind::Object;
  }

  /// The typed accessors assert the kind in Debug and return a default otherwise.
  [[nodiscard]] bool AsBool() const noexcept;
  [[nodiscard]] std::int64_t AsInteger() const noexcept;
  /// A Double, or an Integer converted; the one place an integer becomes a float, for the
  /// renderer-side numbers such as light colours.
  [[nodiscard]] double AsDouble() const noexcept;
  [[nodiscard]] const std::string& AsString() const noexcept;

  // Arrays and objects share a count.
  [[nodiscard]] std::size_t Size() const noexcept
  {
    return m_values.size();
  }

  // Arrays.
  [[nodiscard]] const JsonValue& At(std::size_t _index) const noexcept;
  [[nodiscard]] JsonValue& At(std::size_t _index) noexcept;
  JsonValue& Push(JsonValue _value);

  // Objects: members in insertion order.
  [[nodiscard]] const JsonValue* Find(std::string_view _key) const noexcept;
  [[nodiscard]] JsonValue* Find(std::string_view _key) noexcept;
  [[nodiscard]] const std::string& KeyAt(std::size_t _index) const noexcept
  {
    return m_keys[_index];
  }
  [[nodiscard]] const JsonValue& ValueAt(std::size_t _index) const noexcept
  {
    return m_values[_index];
  }
  /// Sets a member, replacing one of the same key in place; returns the stored value.
  JsonValue& Set(std::string _key, JsonValue _value);

private:
  JsonKind m_kind = JsonKind::Null;
  bool m_bool = false;
  std::int64_t m_integer = 0;
  double m_double = 0.0;
  std::string m_string;
  std::vector<std::string> m_keys;
  std::vector<JsonValue> m_values;
};

struct JsonError
{
  std::string file;
  int line = 0;
  int column = 0;
  std::string message;

  /// "file(line,column): message", the form the build tools print.
  [[nodiscard]] std::string ToString() const;
};

/// The nesting the reader accepts before refusing a document, so that a hostile file cannot
/// overflow the stack.
inline constexpr int JSON_MAX_DEPTH = 128;

/// Parses a whole document. On failure _out is untouched and _error names the file the caller
/// gave, the 1-based line and column of the offending character (or of the end, for a document
/// that stops short), and what was expected.
[[nodiscard]] bool ParseJson(std::string_view _text, std::string_view _file, JsonValue& _out, JsonError& _error);

/// Writes a document the reader accepts: members in insertion order, integers as digits, doubles
/// in their shortest round-trip form, strings escaped. _indent of 0 writes one line.
[[nodiscard]] std::string WriteJson(const JsonValue& _value, int _indent = 2);

} // namespace Neuron
