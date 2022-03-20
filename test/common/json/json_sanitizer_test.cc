#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

class JsonSanitizerTest : public testing::Test {
protected:
  absl::string_view sanitize(absl::string_view str) {
    absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, str);
    std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
        ValueUtil::stringValue(std::string(str)), false, true);
    EXPECT_EQ(proto_sanitized, absl::StrCat("\"", hand_sanitized, "\"")) << "str=" << str;
    return hand_sanitized;
  }

  void expectUnchanged(absl::string_view str) { EXPECT_EQ(str, sanitize(str)); }

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
  EXPECT_EQ("\\b", sanitize("\b"));
  EXPECT_EQ("\\f", sanitize("\f"));
  EXPECT_EQ("\\n", sanitize("\n"));
  EXPECT_EQ("\\r", sanitize("\r"));
  EXPECT_EQ("\\t", sanitize("\t"));
  EXPECT_EQ("\\\\", sanitize("\\"));
  EXPECT_EQ("\\\"", sanitize("\""));
}

TEST_F(JsonSanitizerTest, ControlChars) {
  EXPECT_EQ("\\u0001", sanitize("\001"));
  EXPECT_EQ("\\u0002", sanitize("\002"));
  EXPECT_EQ("\\b", sanitize("\010"));
  EXPECT_EQ("\\t", sanitize("\011"));
  EXPECT_EQ("\\n", sanitize("\012"));
  EXPECT_EQ("\\u000b", sanitize("\013"));
  EXPECT_EQ("\\f", sanitize("\014"));
  EXPECT_EQ("\\r", sanitize("\015"));
  EXPECT_EQ("\\u000e", sanitize("\016"));
  EXPECT_EQ("\\u000f", sanitize("\017"));
  EXPECT_EQ("\\u0010", sanitize("\020"));
  EXPECT_EQ("\\u003c", sanitize("<"));
  EXPECT_EQ("\\u003e", sanitize(">"));
}

TEST_F(JsonSanitizerTest, SevenBitAscii) {
  // Cover all the 7-bit ascii values, calling sanitize so that it checks
  // our hand-rolled sanitizer vs protobuf. We ignore the return-value of
  // sanitize(); we are just calling for it to test against protobuf.
  for (uint32_t i = 0; i < 128; ++i) {
    char c = i;
    sanitize(absl::string_view(&c, 1));
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
  EXPECT_EQ("a\\bc", sanitize("a\bc"));
  EXPECT_EQ("a\\b\\fc", sanitize("a\b\fc"));
  EXPECT_EQ("\\bac", sanitize("\bac"));
  EXPECT_EQ("\\b\\fac", sanitize("\b\fac"));
  EXPECT_EQ("ac\\b", sanitize("ac\b"));
  EXPECT_EQ("ac\\b", sanitize("ac\b"));
}

} // namespace
} // namespace Json
} // namespace Envoy
