#include <ostream>

#include "source/common/json/json_internal.h"
#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StartsWith;

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
  absl::string_view sanitize(absl::string_view str) {
    return TestUtil::stripDoubleQuotes(Envoy::Json::sanitize(buffer_, str));
  }

  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    EXPECT_TRUE(TestUtil::isProtoSerializableUtf8(str)) << "str=" << str;
    absl::string_view sanitized = sanitize(str);
    if (TestUtil::isProtoSerializableUtf8(str)) {
      std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
          ValueUtil::stringValue(std::string(str)), false, true);
      EXPECT_UTF8_EQ(TestUtil::stripDoubleQuotes(proto_sanitized), sanitized, str);
    }
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

  absl::string_view sanitizeInvalid(absl::string_view str) {
    EXPECT_EQ(TestUtil::UnicodeSizePair(0, 0), decode(str));
    return sanitize(str);
  }

  std::pair<uint32_t, uint32_t> decode(absl::string_view str) {
    return TestUtil::decodeUtf8(reinterpret_cast<const uint8_t*>(str.data()), str.size());
  }

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
    buf[0] = byte1 | TestUtil::Utf8_2BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      buf[1] = byte2 | TestUtil::Utf8_ContinuePattern;
      auto [unicode, consumed] =
          Envoy::Json::TestUtil::decodeUtf8(reinterpret_cast<const uint8_t*>(buf), 2);
      ASSERT_EQ(2, consumed);
      sanitizeAndCheckAgainstProtobufJson(utf8);
    }
  }
}

TEST_F(JsonSanitizerTest, AllThreeByteUtf8) {
  std::string utf8("abc");
  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | TestUtil::Utf8_3BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | TestUtil::Utf8_ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | TestUtil::Utf8_ContinuePattern;
        auto [unicode, num_consumed] = TestUtil::decodeUtf8(utf8);

        // 3-byte unicode values start at 0x800. The spec says nothing
        // I can find about 3-byte codes over 0xd800, but neither protobufs
        // or Nlohmann appear to allow those.
        if (unicode >= 0x800 && unicode < 0xd800) {
          absl::string_view sanitized = sanitize(utf8);
          if (TestUtil::isProtoSerializableUtf8(utf8)) {
            auto [unicode, consumed] = TestUtil::decodeUtf8(
                reinterpret_cast<const uint8_t*>(utf8.data()), 3);
            EXPECT_EQ(3, consumed);
            std::string proto_sanitized =
                MessageUtil::getJsonStringFromMessageOrDie(ValueUtil::stringValue(utf8), false, true);
            EXPECT_UTF8_EQ(TestUtil::stripDoubleQuotes(proto_sanitized), sanitized,
                           absl::StrFormat("0x%x(%d,%d,%d)", unicode, byte1, byte2, byte3));
          }
        }
      }
    }
  }
}

// This test takes 17 seconds without optimization.
#ifdef NDEBUG
TEST_F(JsonSanitizerTest, AllFourByteUtf8) {
  std::string utf8("abcd");

  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | TestUtil::Utf8_4BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | TestUtil::Utf8_ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | TestUtil::Utf8_ContinuePattern;
        for (uint32_t byte4 = 0; byte4 < 64; ++byte4) {
          utf8[3] = byte4 | TestUtil::Utf8_ContinuePattern;
          absl::string_view sanitized = sanitize(utf8);
          if (TestUtil::isProtoSerializableUtf8(utf8)) {
            auto [unicode, consumed] = TestUtil::decodeUtf8(
                reinterpret_cast<const uint8_t*>(utf8.data()), 4);
            EXPECT_EQ(4, consumed);
            std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
                ValueUtil::stringValue(utf8), false, true);

            EXPECT_UTF8_EQ(TestUtil::stripDoubleQuotes(proto_sanitized), sanitized,
                           absl::StrFormat("0x%x(%d,%d,%d,%d)",
                                           unicode, byte1, byte2, byte3, byte4));
          }
        }
      }
    }
  }
}
#endif

TEST_F(JsonSanitizerTest, MultiByteUtf8) {
  EXPECT_EQ(TestUtil::UnicodeSizePair(0x3bb, 2), decode(Lambda));
  EXPECT_EQ(TestUtil::UnicodeSizePair(0x3bb, 2), decode(LambdaUtf8));
  EXPECT_EQ(TestUtil::UnicodeSizePair(0x1f79, 3), decode(Omicron));
  EXPECT_EQ(TestUtil::UnicodeSizePair(0x1f79, 3), decode(OmicronUtf8));

  // It's hard to find large unicode characters, but to test the utf8 decoder
  // there are some in https://unicode-table.com/en/blocks/musical-symbols/
  // with reference utf8 encoding from https://unicode-table.com/en/1D11E/
  EXPECT_EQ(TestUtil::UnicodeSizePair(0x1d11e, 4), decode(TrebleClefUtf8));
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

  // Whenever there's an encoding error, the nlohmann json handler throws an
  // exception, which Json::sanitizer catches and just escapes the characters so
  // we don't lose information in the encoding. All bytes with the high-bit set
  // are invalid utf-8 in isolation, so we fall through to escaping these.
  EXPECT_EQ(
      "\\200\\201\\202\\203\\204\\205\\206\\207\\210\\211\\212\\213\\214\\215\\216\\217"
      "\\220\\221\\222\\223\\224\\225\\226\\227\\230\\231\\232\\233\\234\\235\\236\\237"
      "\\240\\241\\242\\243\\244\\245\\246\\247\\250\\251\\252\\253\\254\\255\\256\\257"
      "\\260\\261\\262\\263\\264\\265\\266\\267\\270\\271\\272\\273\\274\\275\\276\\277"
      "\\300\\301\\302\\303\\304\\305\\306\\307\\310\\311\\312\\313\\314\\315\\316\\317"
      "\\320\\321\\322\\323\\324\\325\\326\\327\\330\\331\\332\\333\\334\\335\\336\\337"
      "\\340\\341\\342\\343\\344\\345\\346\\347\\350\\351\\352\\353\\354\\355\\356\\357"
      "\\360\\361\\362\\363\\364\\365\\366\\367\\370\\371\\372\\373\\374\\375\\376\\377",
      sanitize(x80_ff));
}

TEST_F(JsonSanitizerTest, InvalidUtf8) {
  // 2 byte
  EXPECT_EQ("\\316", sanitizeInvalid(truncate(LambdaUtf8)));
  EXPECT_EQ("\\316\\373", sanitizeInvalid(corruptByte2(LambdaUtf8)));

  // 3 byte
  EXPECT_EQ("\\341\\275", sanitizeInvalid(truncate(OmicronUtf8)));
  EXPECT_EQ("\\341\\375\\271", sanitizeInvalid(corruptByte2(OmicronUtf8)));

  // 4 byte
  EXPECT_EQ("\\360\\235\\204", sanitizeInvalid(truncate(TrebleClefUtf8)));
  EXPECT_EQ("\\360\\375\\204\\236", sanitizeInvalid(corruptByte2(TrebleClefUtf8)));

  // Invalid input embedded in normal text.
  EXPECT_EQ(
      "Hello, \\360\\235\\204, World!",
      sanitize(absl::StrCat("Hello, ", truncate(TrebleClefUtf8), ", World!")));

  // Replicate a few other cases that were discovered during initial fuzzing,
  // to ensure we see these as invalid utf8 and avoid them in comparisons.
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("_K\301\234K"));
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("\xF7\xA6\x8A\x8A"));
  EXPECT_FALSE(TestUtil::isProtoSerializableUtf8("\020\377\377\376\000"));
}

} // namespace
} // namespace Json
} // namespace Envoy
