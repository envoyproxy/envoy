#include <ostream>

#include "source/common/json/json_internal.h"
#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"
#include "test/common/json/utf8.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

constexpr absl::string_view Lambda{"λ"};
constexpr absl::string_view LambdaUtf8{"\316\273"};
constexpr absl::string_view Omicron{"ό"};
constexpr absl::string_view OmicronUtf8{"\341\275\271"};
constexpr absl::string_view TrebleClefUtf8{"\360\235\204\236"};

class JsonSanitizerTest : public testing::Test {
protected:
  absl::string_view sanitize(absl::string_view str) { return Envoy::Json::sanitize(buffer_, str); }

  absl::string_view protoSanitize(absl::string_view str) {
    proto_serialization_buffer_ = MessageUtil::getJsonStringFromMessageOrError(
        ValueUtil::stringValue(std::string(str)), false, true);
    return stripDoubleQuotes(proto_serialization_buffer_);
  }

  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    absl::string_view sanitized = sanitize(str);
    EXPECT_TRUE(TestUtil::isProtoSerializableUtf8(str)) << "str=" << str;
    EXPECT_UTF8_EQ(protoSanitize(str), sanitized, str);
    return sanitized;
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

  absl::string_view sanitizeInvalidAndCheckEscapes(absl::string_view str) {
    EXPECT_FALSE(TestUtil::isProtoSerializableUtf8(str));
    absl::string_view sanitized = sanitize(str);
    EXPECT_JSON_STREQ(sanitized, str, str);
    return sanitized;
  }

  std::pair<uint32_t, uint32_t> decode(absl::string_view str) {
    return Utf8::decode(reinterpret_cast<const uint8_t*>(str.data()), str.size());
  }

  std::string buffer_;
  std::string proto_serialization_buffer_;
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
  EXPECT_EQ("<", sanitize("<")); // protobuf serializes to \\u003c
  EXPECT_EQ(">", sanitize(">")); // protobuf serializes to \\u003e
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
    return {reinterpret_cast<const char*>(&chars[0]), chars.size()};
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
    buf[0] = byte1 | Utf8::Pattern2Byte;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      buf[1] = byte2 | Utf8::ContinuePattern;
      auto [unicode, consumed] =
          Envoy::Json::Utf8::decode(reinterpret_cast<const uint8_t*>(buf), 2);
      ASSERT_EQ(2, consumed);
      sanitizeAndCheckAgainstProtobufJson(utf8);
    }
  }
}

TEST_F(JsonSanitizerTest, AllThreeByteUtf8) {
  std::string utf8("abc");
  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | Utf8::Pattern3Byte;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | Utf8::ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | Utf8::ContinuePattern;
        auto [unicode, num_consumed] = Utf8::decode(utf8);
        if (unicode >= 0x800) { // 3-byte unicode values start at 0x800.
          absl::string_view sanitized = sanitize(utf8);
          if (TestUtil::isProtoSerializableUtf8(utf8)) {
            auto [unicode, consumed] =
                Utf8::decode(reinterpret_cast<const uint8_t*>(utf8.data()), 3);
            EXPECT_EQ(3, consumed);
            EXPECT_UTF8_EQ(protoSanitize(utf8), sanitized,
                           absl::StrFormat("0x%x(%d,%d,%d)", unicode, byte1, byte2, byte3));
          } else {
            EXPECT_JSON_STREQ(sanitized, utf8,
                              absl::StrFormat("non-utf8(%d,%d,%d)", byte1, byte2, byte3));
          }
        }
      }
    }
  }
}

TEST_F(JsonSanitizerTest, AllFourByteUtf8) {
  std::string utf8("abcd");

  // This test takes 46 seconds without optimization and 46 seconds without,
  // so we'll just stride all loop by 2 in non-optimized mode to cover the
  // space in under 5 seconds.
#ifdef NDEBUG
  const uint32_t inc = 1;
#else
  const uint32_t inc = 2;
#endif

  for (uint32_t byte1 = 0; byte1 < 16; byte1 += inc) {
    utf8[0] = byte1 | Utf8::Pattern4Byte;
    for (uint32_t byte2 = 0; byte2 < 64; byte2 += inc) {
      utf8[1] = byte2 | Utf8::ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; byte3 += inc) {
        utf8[2] = byte3 | Utf8::ContinuePattern;
        for (uint32_t byte4 = 0; byte4 < 64; byte4 += inc) {
          utf8[3] = byte4 | Utf8::ContinuePattern;
          absl::string_view sanitized = sanitize(utf8);
          if (TestUtil::isProtoSerializableUtf8(utf8)) {
            auto [unicode, consumed] =
                Utf8::decode(reinterpret_cast<const uint8_t*>(utf8.data()), 4);
            EXPECT_EQ(4, consumed);
            EXPECT_UTF8_EQ(
                protoSanitize(utf8), sanitized,
                absl::StrFormat("0x%x(%d,%d,%d,%d)", unicode, byte1, byte2, byte3, byte4));
          } else {
            EXPECT_JSON_STREQ(sanitized, utf8,
                              absl::StrFormat("non-utf8(%d,%d,%d,%d)", byte1, byte2, byte3, byte4));
          }
        }
      }
    }
  }
}

TEST_F(JsonSanitizerTest, MultiByteUtf8) {
  EXPECT_EQ(Utf8::UnicodeSizePair(0x3bb, 2), decode(Lambda));
  EXPECT_EQ(Utf8::UnicodeSizePair(0x3bb, 2), decode(LambdaUtf8));
  EXPECT_EQ(Utf8::UnicodeSizePair(0x1f79, 3), decode(Omicron));
  EXPECT_EQ(Utf8::UnicodeSizePair(0x1f79, 3), decode(OmicronUtf8));

  // It's hard to find large Unicode characters, but to test the utf8 decoder
  // there are some in https://unicode-table.com/en/blocks/musical-symbols/
  // with reference utf8 encoding from https://unicode-table.com/en/1D11E/
  EXPECT_EQ(Utf8::UnicodeSizePair(0x1d11e, 4), decode(TrebleClefUtf8));
}

TEST_F(JsonSanitizerTest, Low8Bit) {
  // The characters from 0 to 0xBF (191) inclusive are all rendered identically
  // to the protobuf JSON encoder.
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

      // < and > are serialized by JSON as Unicode.
      "<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"

      // Remaining 7-bit codes ending with 127.
      "[\\\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\177",

      sanitizeAndCheckAgainstProtobufJson(x0_7f));
}

TEST_F(JsonSanitizerTest, High8Bit) {
  std::string x80_ff;
  for (uint32_t i = 0x80; i <= 0xff; ++i) {
    char ch = i;
    x80_ff.push_back(ch);
  }

  // Whenever there's an encoding error, the nlohmann JSON handler throws an
  // exception, which Json::sanitizer catches and just escapes the characters so
  // we don't lose information in the encoding. All bytes with the high-bit set
  // are invalid utf-8 in isolation, so we fall through to escaping these.
  EXPECT_EQ("\\u0080\\u0081\\u0082\\u0083\\u0084\\u0085\\u0086\\u0087\\u0088\\u0089\\u008a"
            "\\u008b\\u008c\\u008d\\u008e\\u008f\\u0090\\u0091\\u0092\\u0093\\u0094\\u0095"
            "\\u0096\\u0097\\u0098\\u0099\\u009a\\u009b\\u009c\\u009d\\u009e\\u009f\\u00a0"
            "\\u00a1\\u00a2\\u00a3\\u00a4\\u00a5\\u00a6\\u00a7\\u00a8\\u00a9\\u00aa\\u00ab"
            "\\u00ac\\u00ad\\u00ae\\u00af\\u00b0\\u00b1\\u00b2\\u00b3\\u00b4\\u00b5\\u00b6"
            "\\u00b7\\u00b8\\u00b9\\u00ba\\u00bb\\u00bc\\u00bd\\u00be\\u00bf\\u00c0\\u00c1"
            "\\u00c2\\u00c3\\u00c4\\u00c5\\u00c6\\u00c7\\u00c8\\u00c9\\u00ca\\u00cb\\u00cc"
            "\\u00cd\\u00ce\\u00cf\\u00d0\\u00d1\\u00d2\\u00d3\\u00d4\\u00d5\\u00d6\\u00d7"
            "\\u00d8\\u00d9\\u00da\\u00db\\u00dc\\u00dd\\u00de\\u00df\\u00e0\\u00e1\\u00e2"
            "\\u00e3\\u00e4\\u00e5\\u00e6\\u00e7\\u00e8\\u00e9\\u00ea\\u00eb\\u00ec\\u00ed"
            "\\u00ee\\u00ef\\u00f0\\u00f1\\u00f2\\u00f3\\u00f4\\u00f5\\u00f6\\u00f7\\u00f8"
            "\\u00f9\\u00fa\\u00fb\\u00fc\\u00fd\\u00fe\\u00ff",
            sanitize(x80_ff));
}

TEST_F(JsonSanitizerTest, InvalidUtf8) {
  // 2 byte
  EXPECT_EQ("\\u00ce", sanitizeInvalidAndCheckEscapes(truncate(LambdaUtf8)));
  EXPECT_EQ("\\u00ce\\u00fb", sanitizeInvalidAndCheckEscapes(corruptByte2(LambdaUtf8)));

  // 3 byte
  EXPECT_EQ("\\u00e1\\u00bd", sanitizeInvalidAndCheckEscapes(truncate(OmicronUtf8)));
  EXPECT_EQ("\\u00e1\\u00fd\\u00b9", sanitizeInvalidAndCheckEscapes(corruptByte2(OmicronUtf8)));

  // 4 byte
  EXPECT_EQ("\\u00f0\\u009d\\u0084", sanitizeInvalidAndCheckEscapes(truncate(TrebleClefUtf8)));
  EXPECT_EQ("\\u00f0\\u00fd\\u0084\\u009e",
            sanitizeInvalidAndCheckEscapes(corruptByte2(TrebleClefUtf8)));

  // Invalid input embedded in normal text.
  EXPECT_EQ("Hello, \\u00f0\\u009d\\u0084, World!",
            sanitizeInvalidAndCheckEscapes(
                absl::StrCat("Hello, ", truncate(TrebleClefUtf8), ", World!")));

  // Invalid input with leading slash.
  EXPECT_EQ("\\u005cHello, \\u00f0\\u009d\\u0084, World!",
            sanitizeInvalidAndCheckEscapes(
                absl::StrCat("\\Hello, ", truncate(TrebleClefUtf8), ", World!")));

  // Replicate a few other cases that were discovered during initial fuzzing,
  // to ensure we see these as invalid utf8 and avoid them in comparisons.
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("_K\301\234K"));
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("\xF7\xA6\x8A\x8A"));
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("\020\377\377\376\000"));
}

} // namespace
} // namespace Json
} // namespace Envoy
