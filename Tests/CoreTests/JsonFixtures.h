#pragma once

// The conformance set of Core's JSON reader (m0-foundation/T11), written for this repository.
// Every document that must parse, and every document that must fail with the 1-based line and
// column the reader reports for it. Positions follow the reader's rule: the offending character,
// or the end of the document when it stops short.

namespace CoreTests
{

struct AcceptedJson
{
  const char* name;
  const char* text;
};

struct RejectedJson
{
  const char* name;
  const char* text;
  int line;
  int column;
};

inline constexpr AcceptedJson ACCEPTED_JSON[] = {
  {"null", "null"},
  {"true", "true"},
  {"false", "false"},
  {"zero", "0"},
  {"negative zero", "-0"},
  {"integer", "12345"},
  {"negative integer", "-42"},
  {"largest int64", "9223372036854775807"},
  {"smallest int64", "-9223372036854775808"},
  {"beyond int64 becomes a double", "9223372036854775808"},
  {"fraction", "0.5"},
  {"negative fraction", "-1.25"},
  {"exponent", "1e2"},
  {"capital exponent with sign", "1E-2"},
  {"fraction and exponent", "6.02e+23"},
  {"empty string", "\"\""},
  {"plain string", "\"hello\""},
  {"escapes", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\""},
  {"unicode escape", "\"\\u0041\""},
  {"unicode escape non-ascii", "\"\\u00e9\""},
  {"surrogate pair", "\"\\ud83d\\ude00\""},
  {"utf-8 two bytes", "\"\xC3\xA9\""},
  {"utf-8 three bytes", "\"\xE2\x82\xAC\""},
  {"utf-8 four bytes", "\"\xF0\x9F\x98\x80\""},
  {"empty array", "[]"},
  {"empty array with space", "[ ]"},
  {"array of numbers", "[1, 2, 3]"},
  {"nested arrays", "[[[]]]"},
  {"mixed array", "[null, true, 1, \"a\", [], {}]"},
  {"empty object", "{}"},
  {"empty object with space", "{ }"},
  {"one member", "{\"a\": 1}"},
  {"members", "{\"a\": 1, \"b\": [true], \"c\": {\"d\": null}}"},
  {"whitespace everywhere", " \t\r\n{ \"a\" : [ 1 , 2 ] } \n"},
  {"tabs and newlines inside", "{\n\t\"a\":\n\t\t1\n}"},
  {"deep but legal nesting", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"},
  {"long string", "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""},
  {"key with escapes", "{\"a\\nb\": 1}"},
  {"empty key", "{\"\": 1}"},
  {"large exponent", "1e300"},
  {"tiny fraction", "0.000001"},
  {"many digits", "123456789012345.678"},
  {"string with slash", "\"a/b\""},
  {"keys keep order", "{\"z\": 1, \"a\": 2, \"m\": 3}"},
};

inline constexpr RejectedJson REJECTED_JSON[] = {
  {"empty document", "", 1, 1},
  {"only whitespace", "  \n ", 2, 2},
  {"trailing content", "1 2", 1, 3},
  {"trailing comma in array", "[1,]", 1, 4},
  {"trailing comma in object", "{\"a\":1,}", 1, 8},
  {"leading comma", "[,1]", 1, 2},
  {"single quotes", "'a'", 1, 1},
  {"unquoted key", "{a:1}", 1, 2},
  {"missing colon", "{\"a\" 1}", 1, 6},
  {"missing value", "{\"a\":}", 1, 6},
  {"missing comma in array", "[1 2]", 1, 4},
  {"missing comma in object", "{\"a\":1 \"b\":2}", 1, 8},
  {"unterminated array", "[1", 1, 3},
  {"unterminated object", "{\"a\":1", 1, 7},
  {"unterminated string", "\"abc", 1, 5},
  {"bad literal true", "tru", 1, 1},
  {"bad literal null", "nul", 1, 1},
  {"capital literal", "True", 1, 1},
  {"NaN", "NaN", 1, 1},
  {"Infinity", "Infinity", 1, 1},
  {"leading plus", "+1", 1, 1},
  {"leading zero", "01", 1, 2},
  {"minus alone", "-", 1, 2},
  {"minus then letter", "-a", 1, 2},
  {"trailing decimal point", "1.", 1, 3},
  {"decimal point then letter", "1.x", 1, 3},
  {"exponent without digits", "1e", 1, 3},
  {"exponent sign without digits", "1e+", 1, 4},
  {"hex number", "0x10", 1, 2},
  {"comment", "// x\n1", 1, 1},
  {"block comment", "/* x */ 1", 1, 1},
  {"control character in string", "\"a\tb\"", 1, 3},
  {"newline in string", "\"a\nb\"", 1, 3},
  {"unknown escape", "\"\\x\"", 1, 3},
  {"short unicode escape", "\"\\u12\"", 1, 6},
  {"non-hex unicode escape", "\"\\u12G4\"", 1, 6},
  {"lone high surrogate", "\"\\ud83d\"", 1, 2},
  {"lone low surrogate", "\"\\ude00\"", 1, 2},
  {"high surrogate then non-surrogate", "\"\\ud83d\\u0041\"", 1, 2},
  {"invalid utf-8 lead byte", "\"\xFF\"", 1, 2},
  {"truncated utf-8 sequence", "\"\xE2\x82\"", 1, 2},
  {"overlong utf-8", "\"\xC0\x80\"", 1, 2},
  {"utf-8 surrogate", "\"\xED\xA0\x80\"", 1, 2},
  {"duplicate key", "{\"a\":1,\"a\":2}", 1, 8},
  {"bare word", "hello", 1, 1},
  {"object as key", "{{}:1}", 1, 2},
  {"error on a later line", "{\n  \"a\": 1,\n  \"b\": tru\n}", 3, 8},
  {"nested too deeply",
   "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[["
   "[[[[[[[[",
   1, 129},
};

} // namespace CoreTests
