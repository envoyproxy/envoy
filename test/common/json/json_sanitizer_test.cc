#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonSanitizerTest : public testing::Test {
protected:
  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    EXPECT_TRUE(JsonSanitizer::isValidUtf8(str)) << "str=" << str;
    absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, str);
    std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
        ValueUtil::stringValue(std::string(str)), false, true);
    EXPECT_EQ(stripDoubleQuotes(proto_sanitized), hand_sanitized) << "str=" << str;
    return hand_sanitized;
  }

  void expectUnchanged(absl::string_view str) {
    EXPECT_EQ(str, sanitizeAndCheckAgainstProtobufJson(str));
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
      //ENVOY_LOG_MISC(error, "byte1={}, byte2={}", byte1, byte2);
      sanitizeAndCheckAgainstProtobufJson(utf8);
    }
  }
}

TEST_F(JsonSanitizerTest, MultiByteUtf8) {
  using UnicodeSizePair = JsonSanitizer::UnicodeSizePair;
  EXPECT_EQ(UnicodeSizePair(955, 2), decode("λ"));
  EXPECT_EQ(UnicodeSizePair(8057, 3), decode("ό"));

  // It's hard to find large unicode characters, but to test the utf8 decoder
  // there are some in https://unicode-table.com/en/blocks/musical-symbols/
  // with reference utf8 encoding from https://unicode-table.com/en/1D11E/
  EXPECT_EQ(UnicodeSizePair(0x1d11e, 4), decode("\xf0\x9d\x84\x9e")); // treble clef
}

} // namespace
} // namespace Json
} // namespace Envoy
