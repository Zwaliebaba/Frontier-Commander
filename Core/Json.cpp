#include "pch.h"

#include "Json.h"
#include "Assertion.h"

#include <charconv>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

namespace Neuron
{

// --- JsonValue ---------------------------------------------------------------------------------

JsonValue JsonValue::Bool(bool _value) noexcept
{
  JsonValue value;
  value.m_kind = JsonKind::Bool;
  value.m_bool = _value;
  return value;
}

JsonValue JsonValue::Integer(std::int64_t _value) noexcept
{
  JsonValue value;
  value.m_kind = JsonKind::Integer;
  value.m_integer = _value;
  return value;
}

JsonValue JsonValue::Double(double _value) noexcept
{
  JsonValue value;
  value.m_kind = JsonKind::Double;
  value.m_double = _value;
  return value;
}

JsonValue JsonValue::String(std::string _value)
{
  JsonValue value;
  value.m_kind = JsonKind::String;
  value.m_string = std::move(_value);
  return value;
}

JsonValue JsonValue::Array()
{
  JsonValue value;
  value.m_kind = JsonKind::Array;
  return value;
}

JsonValue JsonValue::Object()
{
  JsonValue value;
  value.m_kind = JsonKind::Object;
  return value;
}

bool JsonValue::AsBool() const noexcept
{
  FRONTIER_ASSERT(m_kind == JsonKind::Bool);
  return m_kind == JsonKind::Bool && m_bool;
}

std::int64_t JsonValue::AsInteger() const noexcept
{
  FRONTIER_ASSERT(m_kind == JsonKind::Integer);
  return m_kind == JsonKind::Integer ? m_integer : 0;
}

double JsonValue::AsDouble() const noexcept
{
  FRONTIER_ASSERT(IsNumber());
  if (m_kind == JsonKind::Double)
  {
    return m_double;
  }
  return m_kind == JsonKind::Integer ? static_cast<double>(m_integer) : 0.0;
}

const std::string& JsonValue::AsString() const noexcept
{
  FRONTIER_ASSERT(m_kind == JsonKind::String);
  return m_string;
}

const JsonValue& JsonValue::At(std::size_t _index) const noexcept
{
  FRONTIER_ASSERT(_index < m_values.size());
  return m_values[_index];
}

JsonValue& JsonValue::At(std::size_t _index) noexcept
{
  FRONTIER_ASSERT(_index < m_values.size());
  return m_values[_index];
}

JsonValue& JsonValue::Push(JsonValue _value)
{
  FRONTIER_ASSERT(m_kind == JsonKind::Array);
  m_values.push_back(std::move(_value));
  return m_values.back();
}

const JsonValue* JsonValue::Find(std::string_view _key) const noexcept
{
  for (std::size_t index = 0; index < m_keys.size(); ++index)
  {
    if (m_keys[index] == _key)
    {
      return &m_values[index];
    }
  }
  return nullptr;
}

JsonValue* JsonValue::Find(std::string_view _key) noexcept
{
  for (std::size_t index = 0; index < m_keys.size(); ++index)
  {
    if (m_keys[index] == _key)
    {
      return &m_values[index];
    }
  }
  return nullptr;
}

JsonValue& JsonValue::Set(std::string _key, JsonValue _value)
{
  FRONTIER_ASSERT(m_kind == JsonKind::Object);
  if (JsonValue* existing = Find(_key); existing != nullptr)
  {
    *existing = std::move(_value);
    return *existing;
  }
  m_keys.push_back(std::move(_key));
  m_values.push_back(std::move(_value));
  return m_values.back();
}

std::string JsonError::ToString() const
{
  return file + "(" + std::to_string(line) + "," + std::to_string(column) + "): " + message;
}

// --- The reader --------------------------------------------------------------------------------

namespace
{

class Parser
{
public:
  Parser(std::string_view _text, std::string_view _file, JsonError& _error) noexcept
    : m_text(_text),
      m_file(_file),
      m_error(_error)
  {
  }

  bool ParseDocument(JsonValue& _out)
  {
    SkipWhitespace();
    if (AtEnd())
    {
      return Fail(m_position, "expected a value, found the end of the document");
    }
    JsonValue value;
    if (!ParseValue(value, 0))
    {
      return false;
    }
    SkipWhitespace();
    if (!AtEnd())
    {
      return Fail(m_position, "expected the end of the document after the value");
    }
    _out = std::move(value);
    return true;
  }

private:
  [[nodiscard]] bool AtEnd() const noexcept
  {
    return m_position >= m_text.size();
  }
  [[nodiscard]] unsigned char Peek() const noexcept
  {
    return static_cast<unsigned char>(m_text[m_position]);
  }

  void SkipWhitespace() noexcept
  {
    while (!AtEnd())
    {
      const unsigned char c = Peek();
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      {
        break;
      }
      ++m_position;
    }
  }

  bool Fail(std::size_t _offset, const char* _message)
  {
    if (_offset > m_text.size())
    {
      _offset = m_text.size();
    }
    int line = 1;
    std::size_t lineStart = 0;
    for (std::size_t index = 0; index < _offset; ++index)
    {
      if (m_text[index] == '\n')
      {
        ++line;
        lineStart = index + 1;
      }
    }
    m_error.file = std::string(m_file);
    m_error.line = line;
    m_error.column = static_cast<int>(_offset - lineStart) + 1;
    m_error.message = _message;
    return false;
  }

  bool ParseValue(JsonValue& _out, int _depth)
  {
    if (AtEnd())
    {
      return Fail(m_position, "expected a value, found the end of the document");
    }
    switch (Peek())
    {
    case '{':
      return ParseObject(_out, _depth);
    case '[':
      return ParseArray(_out, _depth);
    case '"':
    {
      std::string text;
      if (!ParseString(text))
      {
        return false;
      }
      _out = JsonValue::String(std::move(text));
      return true;
    }
    case 't':
      return ParseLiteral("true", JsonValue::Bool(true), _out);
    case 'f':
      return ParseLiteral("false", JsonValue::Bool(false), _out);
    case 'n':
      return ParseLiteral("null", JsonValue::Null(), _out);
    default:
      if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9'))
      {
        return ParseNumber(_out);
      }
      return Fail(m_position, "expected a value");
    }
  }

  bool ParseLiteral(const char* _literal, JsonValue _value, JsonValue& _out)
  {
    const std::size_t start = m_position;
    const std::size_t length = std::strlen(_literal);
    if (m_text.size() - m_position < length || m_text.compare(m_position, length, _literal) != 0)
    {
      return Fail(start, "expected a value");
    }
    m_position += length;
    _out = std::move(_value);
    return true;
  }

  bool ParseNumber(JsonValue& _out)
  {
    const std::size_t start = m_position;
    bool integral = true;
    if (Peek() == '-')
    {
      ++m_position;
    }
    if (AtEnd() || Peek() < '0' || Peek() > '9')
    {
      return Fail(m_position, "expected a digit");
    }
    if (Peek() == '0')
    {
      ++m_position;
      if (!AtEnd() && Peek() >= '0' && Peek() <= '9')
      {
        return Fail(m_position, "a number may not have a leading zero");
      }
    }
    else
    {
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
      {
        ++m_position;
      }
    }
    if (!AtEnd() && Peek() == '.')
    {
      integral = false;
      ++m_position;
      if (AtEnd() || Peek() < '0' || Peek() > '9')
      {
        return Fail(m_position, "expected a digit after the decimal point");
      }
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
      {
        ++m_position;
      }
    }
    if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
    {
      integral = false;
      ++m_position;
      if (!AtEnd() && (Peek() == '+' || Peek() == '-'))
      {
        ++m_position;
      }
      if (AtEnd() || Peek() < '0' || Peek() > '9')
      {
        return Fail(m_position, "expected a digit in the exponent");
      }
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
      {
        ++m_position;
      }
    }
    const char* first = m_text.data() + start;
    const char* last = m_text.data() + m_position;
    if (integral)
    {
      std::int64_t integer = 0;
      const std::from_chars_result result = std::from_chars(first, last, integer);
      if (result.ec == std::errc() && result.ptr == last)
      {
        _out = JsonValue::Integer(integer);
        return true;
      }
      // Out of the 64-bit range: it is still a number, and it becomes a double.
    }
    double real = 0.0;
    const std::from_chars_result result = std::from_chars(first, last, real);
    if (result.ec != std::errc() || result.ptr != last)
    {
      return Fail(start, "the number is out of range");
    }
    _out = JsonValue::Double(real);
    return true;
  }

  [[nodiscard]] static int HexDigit(unsigned char _c) noexcept
  {
    if (_c >= '0' && _c <= '9')
    {
      return _c - '0';
    }
    if (_c >= 'a' && _c <= 'f')
    {
      return _c - 'a' + 10;
    }
    if (_c >= 'A' && _c <= 'F')
    {
      return _c - 'A' + 10;
    }
    return -1;
  }

  bool ParseHex4(std::uint32_t& _out)
  {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index)
    {
      if (AtEnd())
      {
        return Fail(m_text.size(), "expected four hexadecimal digits");
      }
      const int digit = HexDigit(Peek());
      if (digit < 0)
      {
        return Fail(m_position, "expected a hexadecimal digit");
      }
      value = (value << 4) | static_cast<std::uint32_t>(digit);
      ++m_position;
    }
    _out = value;
    return true;
  }

  static void AppendUtf8(std::string& _out, std::uint32_t _codePoint)
  {
    if (_codePoint < 0x80)
    {
      _out.push_back(static_cast<char>(_codePoint));
    }
    else if (_codePoint < 0x800)
    {
      _out.push_back(static_cast<char>(0xC0 | (_codePoint >> 6)));
      _out.push_back(static_cast<char>(0x80 | (_codePoint & 0x3F)));
    }
    else if (_codePoint < 0x10000)
    {
      _out.push_back(static_cast<char>(0xE0 | (_codePoint >> 12)));
      _out.push_back(static_cast<char>(0x80 | ((_codePoint >> 6) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | (_codePoint & 0x3F)));
    }
    else
    {
      _out.push_back(static_cast<char>(0xF0 | (_codePoint >> 18)));
      _out.push_back(static_cast<char>(0x80 | ((_codePoint >> 12) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | ((_codePoint >> 6) & 0x3F)));
      _out.push_back(static_cast<char>(0x80 | (_codePoint & 0x3F)));
    }
  }

  /// Validates one UTF-8 sequence starting at the current position and copies it; overlong
  /// forms, surrogates and code points above U+10FFFF are refused.
  bool CopyUtf8Sequence(std::string& _out)
  {
    const unsigned char lead = Peek();
    std::size_t length = 0;
    std::uint32_t codePoint = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF)
    {
      length = 2;
      codePoint = lead & 0x1F;
      minimum = 0x80;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
      length = 3;
      codePoint = lead & 0x0F;
      minimum = 0x800;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
      length = 4;
      codePoint = lead & 0x07;
      minimum = 0x10000;
    }
    else
    {
      return Fail(m_position, "invalid UTF-8");
    }
    if (m_text.size() - m_position < length)
    {
      return Fail(m_position, "invalid UTF-8");
    }
    for (std::size_t index = 1; index < length; ++index)
    {
      const unsigned char next = static_cast<unsigned char>(m_text[m_position + index]);
      if ((next & 0xC0) != 0x80)
      {
        return Fail(m_position, "invalid UTF-8");
      }
      codePoint = (codePoint << 6) | (next & 0x3F);
    }
    if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
    {
      return Fail(m_position, "invalid UTF-8");
    }
    _out.append(m_text.data() + m_position, length);
    m_position += length;
    return true;
  }

  bool ParseString(std::string& _out)
  {
    FRONTIER_ASSERT(Peek() == '"');
    ++m_position;
    std::string text;
    for (;;)
    {
      if (AtEnd())
      {
        return Fail(m_text.size(), "the string is not terminated");
      }
      const unsigned char c = Peek();
      if (c == '"')
      {
        ++m_position;
        _out = std::move(text);
        return true;
      }
      if (c < 0x20)
      {
        return Fail(m_position, "a control character in a string must be escaped");
      }
      if (c >= 0x80)
      {
        if (!CopyUtf8Sequence(text))
        {
          return false;
        }
        continue;
      }
      if (c != '\\')
      {
        text.push_back(static_cast<char>(c));
        ++m_position;
        continue;
      }
      const std::size_t escapeStart = m_position;
      ++m_position;
      if (AtEnd())
      {
        return Fail(m_text.size(), "the string is not terminated");
      }
      const unsigned char escaped = Peek();
      ++m_position;
      switch (escaped)
      {
      case '"':
        text.push_back('"');
        break;
      case '\\':
        text.push_back('\\');
        break;
      case '/':
        text.push_back('/');
        break;
      case 'b':
        text.push_back('\b');
        break;
      case 'f':
        text.push_back('\f');
        break;
      case 'n':
        text.push_back('\n');
        break;
      case 'r':
        text.push_back('\r');
        break;
      case 't':
        text.push_back('\t');
        break;
      case 'u':
      {
        std::uint32_t codePoint = 0;
        if (!ParseHex4(codePoint))
        {
          return false;
        }
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
        {
          // A high surrogate must be followed by an escaped low surrogate.
          if (m_text.size() - m_position < 2 || m_text[m_position] != '\\' || m_text[m_position + 1] != 'u')
          {
            return Fail(escapeStart, "a high surrogate must be followed by a low surrogate");
          }
          m_position += 2;
          std::uint32_t low = 0;
          if (!ParseHex4(low))
          {
            return false;
          }
          if (low < 0xDC00 || low > 0xDFFF)
          {
            return Fail(escapeStart, "a high surrogate must be followed by a low surrogate");
          }
          codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
        {
          return Fail(escapeStart, "a lone low surrogate");
        }
        AppendUtf8(text, codePoint);
        break;
      }
      default:
        return Fail(m_position - 1, "unknown escape");
      }
    }
  }

  bool ParseArray(JsonValue& _out, int _depth)
  {
    if (_depth >= JSON_MAX_DEPTH)
    {
      return Fail(m_position, "the document nests too deeply");
    }
    ++m_position; // '['
    JsonValue array = JsonValue::Array();
    SkipWhitespace();
    if (!AtEnd() && Peek() == ']')
    {
      ++m_position;
      _out = std::move(array);
      return true;
    }
    for (;;)
    {
      SkipWhitespace();
      JsonValue element;
      if (!ParseValue(element, _depth + 1))
      {
        return false;
      }
      array.Push(std::move(element));
      SkipWhitespace();
      if (AtEnd())
      {
        return Fail(m_text.size(), "expected ',' or ']'");
      }
      if (Peek() == ',')
      {
        ++m_position;
        continue;
      }
      if (Peek() == ']')
      {
        ++m_position;
        _out = std::move(array);
        return true;
      }
      return Fail(m_position, "expected ',' or ']'");
    }
  }

  bool ParseObject(JsonValue& _out, int _depth)
  {
    if (_depth >= JSON_MAX_DEPTH)
    {
      return Fail(m_position, "the document nests too deeply");
    }
    ++m_position; // '{'
    JsonValue object = JsonValue::Object();
    SkipWhitespace();
    if (!AtEnd() && Peek() == '}')
    {
      ++m_position;
      _out = std::move(object);
      return true;
    }
    for (;;)
    {
      SkipWhitespace();
      if (AtEnd() || Peek() != '"')
      {
        return Fail(m_position, "expected a string key");
      }
      const std::size_t keyStart = m_position;
      std::string key;
      if (!ParseString(key))
      {
        return false;
      }
      if (object.Find(key) != nullptr)
      {
        return Fail(keyStart, "duplicate key");
      }
      SkipWhitespace();
      if (AtEnd() || Peek() != ':')
      {
        return Fail(m_position, "expected ':'");
      }
      ++m_position;
      SkipWhitespace();
      JsonValue value;
      if (!ParseValue(value, _depth + 1))
      {
        return false;
      }
      object.Set(std::move(key), std::move(value));
      SkipWhitespace();
      if (AtEnd())
      {
        return Fail(m_text.size(), "expected ',' or '}'");
      }
      if (Peek() == ',')
      {
        ++m_position;
        continue;
      }
      if (Peek() == '}')
      {
        ++m_position;
        _out = std::move(object);
        return true;
      }
      return Fail(m_position, "expected ',' or '}'");
    }
  }

  std::string_view m_text;
  std::string_view m_file;
  JsonError& m_error;
  std::size_t m_position = 0;
};

// --- The writer --------------------------------------------------------------------------------

void WriteString(std::string& _out, std::string_view _text)
{
  _out.push_back('"');
  for (const char c : _text)
  {
    const unsigned char byte = static_cast<unsigned char>(c);
    switch (byte)
    {
    case '"':
      _out += "\\\"";
      break;
    case '\\':
      _out += "\\\\";
      break;
    case '\b':
      _out += "\\b";
      break;
    case '\f':
      _out += "\\f";
      break;
    case '\n':
      _out += "\\n";
      break;
    case '\r':
      _out += "\\r";
      break;
    case '\t':
      _out += "\\t";
      break;
    default:
      if (byte < 0x20)
      {
        static constexpr char HEX[] = "0123456789abcdef";
        _out += "\\u00";
        _out.push_back(HEX[byte >> 4]);
        _out.push_back(HEX[byte & 0x0F]);
      }
      else
      {
        _out.push_back(c);
      }
      break;
    }
  }
  _out.push_back('"');
}

void WriteNumber(std::string& _out, double _value)
{
  FRONTIER_ASSERT(_value == _value && _value <= std::numeric_limits<double>::max() && _value >= std::numeric_limits<double>::lowest());
  char buffer[64];
  const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof buffer, _value);
  FRONTIER_ASSERT(result.ec == std::errc());
  const std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
  _out.append(text);
  // The shortest round-trip form of a whole double has no point or exponent, and would be
  // read back as an integer; the point keeps the kind stable across a write and a read.
  if (text.find_first_of(".eE") == std::string_view::npos)
  {
    _out.append(".0");
  }
}

void WriteNumber(std::string& _out, std::int64_t _value)
{
  char buffer[32];
  const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof buffer, _value);
  FRONTIER_ASSERT(result.ec == std::errc());
  _out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

void NewLine(std::string& _out, int _indent, int _level)
{
  if (_indent <= 0)
  {
    return;
  }
  _out.push_back('\n');
  _out.append(static_cast<std::size_t>(_indent) * static_cast<std::size_t>(_level), ' ');
}

void WriteValue(std::string& _out, const JsonValue& _value, int _indent, int _level)
{
  switch (_value.Kind())
  {
  case JsonKind::Null:
    _out += "null";
    break;
  case JsonKind::Bool:
    _out += _value.AsBool() ? "true" : "false";
    break;
  case JsonKind::Integer:
    WriteNumber(_out, _value.AsInteger());
    break;
  case JsonKind::Double:
    WriteNumber(_out, _value.AsDouble());
    break;
  case JsonKind::String:
    WriteString(_out, _value.AsString());
    break;
  case JsonKind::Array:
    if (_value.Size() == 0)
    {
      _out += "[]";
      break;
    }
    _out.push_back('[');
    for (std::size_t index = 0; index < _value.Size(); ++index)
    {
      if (index > 0)
      {
        _out.push_back(',');
      }
      NewLine(_out, _indent, _level + 1);
      WriteValue(_out, _value.At(index), _indent, _level + 1);
    }
    NewLine(_out, _indent, _level);
    _out.push_back(']');
    break;
  case JsonKind::Object:
    if (_value.Size() == 0)
    {
      _out += "{}";
      break;
    }
    _out.push_back('{');
    for (std::size_t index = 0; index < _value.Size(); ++index)
    {
      if (index > 0)
      {
        _out.push_back(',');
      }
      NewLine(_out, _indent, _level + 1);
      WriteString(_out, _value.KeyAt(index));
      _out += _indent > 0 ? ": " : ":";
      WriteValue(_out, _value.ValueAt(index), _indent, _level + 1);
    }
    NewLine(_out, _indent, _level);
    _out.push_back('}');
    break;
  }
}

} // namespace

bool ParseJson(std::string_view _text, std::string_view _file, JsonValue& _out, JsonError& _error)
{
  Parser parser(_text, _file, _error);
  return parser.ParseDocument(_out);
}

std::string WriteJson(const JsonValue& _value, int _indent)
{
  std::string out;
  WriteValue(out, _value, _indent, 0);
  return out;
}

} // namespace Neuron
