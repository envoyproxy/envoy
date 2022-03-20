#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonSanitizerTest : public testing::Test {
protected:
  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, str);
    std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
        ValueUtil::stringValue(std::string(str)), false, true);
    EXPECT_EQ(proto_sanitized, absl::StrCat("\"", hand_sanitized, "\"")) << "str=" << str;
    return hand_sanitized;
  }

  void expectUnchanged(absl::string_view str) {
    EXPECT_EQ(str, sanitizeAndCheckAgainstProtobufJson(str));
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

  expectUnchanged(unicode({0xc2, 0xa2})); // Cent.
  expectUnchanged(unicode({0xc2, 0xa9})); // Copyright.
  expectUnchanged(unicode({0xc3, 0xa0})); // 'a' with accent grave.
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

} // namespace
} // namespace Json
} // namespace Envoy
