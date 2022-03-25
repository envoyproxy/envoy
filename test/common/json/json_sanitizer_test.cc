#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

constexpr absl::string_view Lambda{"λ"};
constexpr absl::string_view LambdaUtf8{"\xce\xbb"};
constexpr absl::string_view Omicron{"ό"};
constexpr absl::string_view OmicronUtf8{"\xe1\xbd\xb9"};
constexpr absl::string_view TrebleClefUtf8{"\xf0\x9d\x84\x9e"};

class JsonSanitizerTest : public testing::Test {
protected:
  using UnicodeSizePair = JsonSanitizer::UnicodeSizePair;

  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    EXPECT_TRUE(isValidUtf8(str)) << "str=" << str;
    absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, str);
    std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
        ValueUtil::stringValue(std::string(str)), false, true);
    EXPECT_EQ(stripDoubleQuotes(proto_sanitized), hand_sanitized) << "str=" << str;
    return hand_sanitized;
  }

  void expectUnchanged(absl::string_view str) {
    EXPECT_EQ(str, sanitizeAndCheckAgainstProtobufJson(str));
  }

  absl::string_view truncate(absl::string_view str) { return str.substr(0, str.size() - 1); }

  std::string corruptByte2(absl::string_view str) {
    std::string corrupt_second_byte = std::string(str);
    ASSERT(str.size() >= 2);
    corrupt_second_byte[1] |= '\xf0';
    return corrupt_second_byte;
  }

  absl::string_view sanitizeInvalid(absl::string_view str) {
    EXPECT_EQ(UnicodeSizePair(0, 0), decode(str));
    return sanitizer_.sanitize(buffer_, str);
  }

  std::pair<uint32_t, uint32_t> decode(absl::string_view str) {
    return JsonSanitizer::decodeUtf8(reinterpret_cast<const uint8_t*>(str.data()), str.size());
  }

  JsonSanitizer sanitizer_;
  std::string buffer_;
};

TEST_F(JsonSanitizerTest, Empty) { expectUnchanged(""); }

TEST_F(JsonSanitizerTest, NoEscape) {
  expectUnchanged("abcdefghijklmnopqrstuvwxyz");
  expectUnchanged("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  expectUnchanged("1234567890");
  expectUnchanged(" `~!@#$%^&*()_+-={}|[]");
  expectUnchanged("Hello world, Καλημέρα κόσμε, コンニチハ");
}

TEST_F(JsonSanitizerTest, SlashChars) {
  EXPECT_EQ("\\b", sanitizeAndCheckAgainstProtobufJson("\b"));
  EXPECT_EQ("\\f", sanitizeAndCheckAgainstProtobufJson("\f"));
  EXPECT_EQ("\\n", sanitizeAndCheckAgainstProtobufJson("\n"));
  EXPECT_EQ("\\r", sanitizeAndCheckAgainstProtobufJson("\r"));
  EXPECT_EQ("\\t", sanitizeAndCheckAgainstProtobufJson("\t"));
  EXPECT_EQ("\\\\", sanitizeAndCheckAgainstProtobufJson("\\"));
  EXPECT_EQ("\\\"", sanitizeAndCheckAgainstProtobufJson("\""));
}

TEST_F(JsonSanitizerTest, ControlChars) {
  EXPECT_EQ("\\u0001", sanitizeAndCheckAgainstProtobufJson("\001"));
  EXPECT_EQ("\\u0002", sanitizeAndCheckAgainstProtobufJson("\002"));
  EXPECT_EQ("\\b", sanitizeAndCheckAgainstProtobufJson("\010"));
  EXPECT_EQ("\\t", sanitizeAndCheckAgainstProtobufJson("\011"));
  EXPECT_EQ("\\n", sanitizeAndCheckAgainstProtobufJson("\012"));
  EXPECT_EQ("\\u000b", sanitizeAndCheckAgainstProtobufJson("\013"));
  EXPECT_EQ("\\f", sanitizeAndCheckAgainstProtobufJson("\014"));
  EXPECT_EQ("\\r", sanitizeAndCheckAgainstProtobufJson("\015"));
  EXPECT_EQ("\\u000e", sanitizeAndCheckAgainstProtobufJson("\016"));
  EXPECT_EQ("\\u000f", sanitizeAndCheckAgainstProtobufJson("\017"));
  EXPECT_EQ("\\u0010", sanitizeAndCheckAgainstProtobufJson("\020"));
  EXPECT_EQ("\\u003c", sanitizeAndCheckAgainstProtobufJson("<"));
  EXPECT_EQ("\\u003e", sanitizeAndCheckAgainstProtobufJson(">"));
}

TEST_F(JsonSanitizerTest, SevenBitAscii) {
  // Cover all the 7-bit ascii values, calling sanitize so that it checks
  // our hand-rolled sanitizer vs protobuf. We ignore the return-value of
  // sanitize(); we are just calling for it to test against protobuf.
  for (uint32_t i = 0; i < 128; ++i) {
    char c = i;
    sanitizeAndCheckAgainstProtobufJson(absl::string_view(&c, 1));
  }
}

TEST_F(JsonSanitizerTest, Utf8) {
  // reference; https://www.charset.org/utf-8
  auto unicode = [](std::vector<uint8_t> chars) -> std::string {
    return std::string(reinterpret_cast<const char*>(&chars[0]), chars.size());
  };

  sanitizeAndCheckAgainstProtobufJson(unicode({0xc2, 0xa2})); // Cent.
  sanitizeAndCheckAgainstProtobufJson(unicode({0xc2, 0xa9})); // Copyright.
  sanitizeAndCheckAgainstProtobufJson(unicode({0xc3, 0xa0})); // 'a' with accent grave.
}

TEST_F(JsonSanitizerTest, Interspersed) {
  EXPECT_EQ("a\\bc", sanitizeAndCheckAgainstProtobufJson("a\bc"));
  EXPECT_EQ("a\\b\\fc", sanitizeAndCheckAgainstProtobufJson("a\b\fc"));
  EXPECT_EQ("\\bac", sanitizeAndCheckAgainstProtobufJson("\bac"));
  EXPECT_EQ("\\b\\fac", sanitizeAndCheckAgainstProtobufJson("\b\fac"));
  EXPECT_EQ("ac\\b", sanitizeAndCheckAgainstProtobufJson("ac\b"));
  EXPECT_EQ("ac\\b", sanitizeAndCheckAgainstProtobufJson("ac\b"));
  EXPECT_EQ("\\ra\\f", sanitizeAndCheckAgainstProtobufJson("\ra\f"));
}

TEST_F(JsonSanitizerTest, AllTwoByteUtf8) {
  char buf[2];
  absl::string_view utf8(buf, 2);
  for (uint32_t byte1 = 2; byte1 < 32; ++byte1) {
    buf[0] = byte1 | JsonSanitizer::Utf8_2BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      buf[1] = byte2 | JsonSanitizer::Utf8_ContinuePattern;
      sanitizeAndCheckAgainstProtobufJson(utf8);
    }
  }
}

TEST_F(JsonSanitizerTest, MultiByteUtf8) {
  EXPECT_EQ(UnicodeSizePair(0x3bb, 2), decode(Lambda));
  EXPECT_EQ(UnicodeSizePair(0x3bb, 2), decode(LambdaUtf8));
  EXPECT_EQ(UnicodeSizePair(0x1f79, 3), decode(Omicron));
  EXPECT_EQ(UnicodeSizePair(0x1f79, 3), decode(OmicronUtf8));

  // It's hard to find large unicode characters, but to test the utf8 decoder
  // there are some in https://unicode-table.com/en/blocks/musical-symbols/
  // with reference utf8 encoding from https://unicode-table.com/en/1D11E/
  EXPECT_EQ(UnicodeSizePair(0x1d11e, 4), decode(TrebleClefUtf8));
}

TEST_F(JsonSanitizerTest, Low8Bit) {
  // The characters from 0 to 0xBF (191) inclusive are all rendered identically
  // to the protobuf json encoder.
  std::string x0_7f;
  for (uint32_t i = 0; i <= 0x7f; ++i) {
    char ch = i;
    x0_7f.push_back(ch);
  }
  EXPECT_EQ(
      // Control-characters 0-31
      "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n"
      "\\u000b\\f\\r\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015"
      "\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f"

      // Printable characters starting with space. Double-quote is back-slashed.
      " !\\\"#$%&'()*+,-./0123456789:;"

      // < and > are serialized by json as unicode.
      "\\u003c=\\u003e?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"

      // Remaining 7-bit codes ending with 127, which is rendered as a unicode escape.
      "[\\\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\\u007f",

      sanitizeAndCheckAgainstProtobufJson(x0_7f));
}

TEST_F(JsonSanitizerTest, High8Bit) {
  std::string x80_ff;
  for (uint32_t i = 0x80; i <= 0xff; ++i) {
    char ch = i;
    x80_ff.push_back(ch);
  }
  // The characters from 0x80 (192) to 255 all start out like they are
  // multi-byte utf-8 sequences, but in this context are not followed by the
  // right continuation pattern. The protobuf json serializer generates
  // lots of error messages for these and yields empty strings, but we
  // just escape them as single bytes.
  EXPECT_EQ(
      // The codes from 128-159 (0x9f) are rendered as several ways: unicode
      // escapes or literal 8-bit characters.
      "\\u0080\\u0081\\u0082\\u0083\\u0084\\u0085\\u0086\\u0087\\u0088\\u0089"
      "\\u008a\\u008b\\u008c\\u008d\\u008e\\u008f\\u0090\\u0091\\u0092\\u0093"
      "\\u0094\\u0095\\u0096\\u0097\\u0098\\u0099\\u009a\\u009b\\u009c\\u009d"
      "\\u009e\\u009f"

      // Then a sequence of literal 8-bit characters.
      "\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC"

      // Weird special-case behavior to match json sanitizer
      "\\u00ad"

      // More literal 8-bit characters.
      "\xAE\xAF\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC"
      "\xBD\xBE\xBF"

      // Codes with a utf8 introductory byte pattern that lack the correct
      // pattern for the remaining codes. These get hex-escaped by the json
      // sanitizer, whereas the protobuf serializer generates an error message
      // and returns an empty string.
      "\\xc0\\xc1\\xc2\\xc3\\xc4\\xc5\\xc6\\xc7\\xc8\\xc9\\xca"
      "\\xcb\\xcc\\xcd\\xce\\xcf\\xd0\\xd1\\xd2\\xd3\\xd4\\xd5\\xd6\\xd7\\xd8"
      "\\xd9\\xda\\xdb\\xdc\\xdd\\xde\\xdf\\xe0\\xe1\\xe2\\xe3\\xe4\\xe5\\xe6"
      "\\xe7\\xe8\\xe9\\xea\\xeb\\xec\\xed\\xee\\xef\\xf0\\xf1\\xf2\\xf3\\xf4"
      "\\xf5\\xf6\\xf7\\xf8\\xf9\\xfa\\xfb\\xfc\\xfd\\xfe\\xff",
      sanitizer_.sanitize(buffer_, x80_ff));
}

TEST_F(JsonSanitizerTest, InvalidUtf8) {
  // 2 byte
  EXPECT_EQ("\\xce", sanitizeInvalid(truncate(LambdaUtf8)));
  EXPECT_EQ("\\xce\\xfb", sanitizeInvalid(corruptByte2(LambdaUtf8)));

  // 3 byte
  EXPECT_EQ("\\xe1\xbd", sanitizeInvalid(truncate(OmicronUtf8)));
  EXPECT_EQ("\\xe1\\xfd\xB9", sanitizeInvalid(corruptByte2(OmicronUtf8)));

  // 4 byte
  EXPECT_EQ("\\xf0\\u009d\\u0084", sanitizeInvalid(truncate(TrebleClefUtf8)));
  EXPECT_EQ("\\xf0\\xfd\\u0084\\u009e", sanitizeInvalid(corruptByte2(TrebleClefUtf8)));

  // Invalid input embedded in normal text.
  EXPECT_EQ(
      "Hello, \\xf0\\u009d\\u0084, World!",
      sanitizer_.sanitize(buffer_, absl::StrCat("Hello, ", truncate(TrebleClefUtf8), ", World!")));
}

} // namespace
} // namespace Json
} // namespace Envoy
