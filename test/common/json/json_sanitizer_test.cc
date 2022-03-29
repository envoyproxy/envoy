#include <ostream>

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
  using UnicodeSizePair = JsonSanitizer::UnicodeSizePair;

  JsonSanitizerTest() {
    if (::getenv("GENERATE_INVALID_UTF8_RANGES") != nullptr) {
      generate_invalid_utf8_ranges_ = true;
      static bool message_emitted = false;
      if (!message_emitted) {
        std::cout << "Runs full sweep of 3-byte and 4-byte utf8 to find unicodes that protobufs "
                     "cannot serialize, to collect them in ranges. The range initialization can "
                     "then be pasted into json_sanitizer_test_util.cc so that future fuzz tests "
                     "and unit tests can avoid doing differentials against protobuf ranges that "
                     "cannot be support. This likely needs to be re-run when the protobufs "
                     "dependency is updated. Be sure to run this piping the output through "
                     " |& grep -v 'contains invalid UTF-8' as the protobuf library will generate "
                     " that message thousands of times and there is no way to disable it."
                  << std::endl;
        message_emitted = true;
      }
    }
  }

  absl::string_view sanitizeAndCheckAgainstProtobufJson(absl::string_view str) {
    EXPECT_TRUE(isValidUtf8(str, true)) << "str=" << str;
    absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, str);
    if (isValidUtf8(str, true)) {
      std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
          ValueUtil::stringValue(std::string(str)), false, true);
      EXPECT_EQ(stripDoubleQuotes(proto_sanitized), hand_sanitized) << "str=" << str;
    }
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
  bool generate_invalid_utf8_ranges_{false};
};

// Collects unicode values that cannot be handled by the protobuf json encoder.
// This is not needed for correct operation of the json sanitizer, but it is
// needed for comparing sanitization results against the proto serializer, and
// for differential fuzzing. We need to avoid comparing sanitization results for
// strings containing utf-8 sequences that protobufs cannot serialize.
//
// Normally when running tests, nothing will be passed to collect(), and emit()
// will return false. But if the protobuf library changes and different unicode
// sets become invalid, we can re-run the collector with:
//
// bazel build -c opt test/common/json:json_sanitizer_test
// GENERATE_INVALID_UTF8_RANGES=1 \
//   ./bazel-bin/test/common/json/json_sanitizer_test |&
//   grep -v 'contains invalid UTF-8'
//
// The grep pipe is essential as otherwise you will be buried in thousands of
// messages from the protobuf library that cannot otherwise be trapped. The
// "-c opt" is essential because JsonSanitizerTest.AllFourByteUtf8 iterates over
// all 4-byte sequences which takes almost 20 seconds without optimization, so
// it is conditionally compiled on NDEBUG.
//
// Running in this mode causes two tests to fail, but prints two initialization
// blocks for invalid byte code ranges, which can then be pasted into the
// InvalidUnicodeSet constructor in json_sanitizer_test_util.cc.
class InvalidUnicodeCollector {
public:
  /**
   * Collects a unicode value that cannot be parsed as utf8 by the protobuf serializer.
   *
   * @param unicode the unicode value
   */
  void collect(uint32_t unicode) { invalid_.insert(unicode, unicode + 1); }

  /**
   * Emits the collection of invalid unicode ranges to stdout.
   *
   * @return true if any invalid ranges were found.
   */
  bool emit(absl::string_view variable_name) {
    bool has_invalid = false;
    for (IntervalSet<uint32_t>::Interval& interval : invalid_.toVector()) {
      has_invalid = true;
      std::cout << absl::StrFormat("    %s.insert(0x%x, 0x%x);\n", variable_name, interval.first,
                                   interval.second);
    }
    return has_invalid;
  }

private:
  IntervalSetImpl<uint32_t> invalid_;
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
      auto [unicode, consumed] =
          Envoy::Json::JsonSanitizer::decodeUtf8(reinterpret_cast<const uint8_t*>(buf), 2);
      ASSERT_EQ(2, consumed);
      sanitizeAndCheckAgainstProtobufJson(utf8);
    }
  }
}

TEST_F(JsonSanitizerTest, AllThreeByteUtf8) {
  std::string utf8("abc");
  InvalidUnicodeCollector invalid;

  bool exclude_protobuf_exceptions = !generate_invalid_utf8_ranges_;

  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | JsonSanitizer::Utf8_3BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | JsonSanitizer::Utf8_ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | JsonSanitizer::Utf8_ContinuePattern;
        if (isValidUtf8(utf8, exclude_protobuf_exceptions)) {
          auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(
              reinterpret_cast<const uint8_t*>(utf8.data()), 3);
          EXPECT_EQ(3, consumed);
          absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, utf8);
          std::string proto_sanitized =
              MessageUtil::getJsonStringFromMessageOrDie(ValueUtil::stringValue(utf8), false, true);
          if (stripDoubleQuotes(proto_sanitized) != hand_sanitized) {
            invalid.collect(unicode);
          }
        }
      }
    }
  }

  EXPECT_FALSE(invalid.emit("invalid_3byte_intervals_"));
}

// This test takes 17 seconds without optimization.
#ifdef NDEBUG
TEST_F(JsonSanitizerTest, AllFourByteUtf8) {
  std::string utf8("abcd");
  InvalidUnicodeCollector invalid;

  bool exclude_protobuf_exceptions = !generate_invalid_utf8_ranges_;

  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | JsonSanitizer::Utf8_4BytePattern;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | JsonSanitizer::Utf8_ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | JsonSanitizer::Utf8_ContinuePattern;
        for (uint32_t byte4 = 0; byte4 < 64; ++byte4) {
          utf8[3] = byte4 | JsonSanitizer::Utf8_ContinuePattern;
          if (isValidUtf8(utf8, exclude_protobuf_exceptions)) {
            absl::string_view hand_sanitized = sanitizer_.sanitize(buffer_, utf8);
            std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrDie(
                ValueUtil::stringValue(utf8), false, true);
            if (stripDoubleQuotes(proto_sanitized) != hand_sanitized) {
              auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(
                  reinterpret_cast<const uint8_t*>(utf8.data()), 4);
              EXPECT_EQ(4, consumed);
              invalid.collect(unicode);
            }
          }
        }
      }
    }
  }

  EXPECT_FALSE(invalid.emit("invalid_4byte_intervals_"));
}
#endif

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
      // pattern for the remaining codes. These get OCT-escaped by the json
      // sanitizer, whereas the protobuf serializer generates an error message
      // and returns an empty string.
      "\\300\\301\\302\\303\\304\\305\\306\\307\\310\\311\\312\\313\\314\\315\\316\\317"
      "\\320\\321\\322\\323\\324\\325\\326\\327\\330\\331\\332\\333\\334\\335\\336\\337"
      "\\340\\341\\342\\343\\344\\345\\346\\347\\350\\351\\352\\353\\354\\355\\356\\357"
      "\\360\\361\\362\\363\\364\\365\\366\\367\\370\\371\\372\\373\\374\\375\\376\\377",
      sanitizer_.sanitize(buffer_, x80_ff));
}

TEST_F(JsonSanitizerTest, InvalidUtf8) {
  // 2 byte
  EXPECT_EQ("\\316", sanitizeInvalid(truncate(LambdaUtf8)));
  EXPECT_EQ("\\316\\373", sanitizeInvalid(corruptByte2(LambdaUtf8)));

  // 3 byte
  absl::string_view out = sanitizeInvalid(truncate(OmicronUtf8));
  EXPECT_THAT(out, StartsWith("\\341"));
  EXPECT_EQ(5, out.size());
  EXPECT_EQ('\275', out[4]);
  EXPECT_EQ("\\341\\375\271", sanitizeInvalid(corruptByte2(OmicronUtf8)));

  // 4 byte
  EXPECT_EQ("\\360\\u009d\\u0084", sanitizeInvalid(truncate(TrebleClefUtf8)));
  EXPECT_EQ("\\360\\375\\u0084\\u009e", sanitizeInvalid(corruptByte2(TrebleClefUtf8)));

  // Invalid input embedded in normal text.
  EXPECT_EQ(
      "Hello, \\360\\u009d\\u0084, World!",
      sanitizer_.sanitize(buffer_, absl::StrCat("Hello, ", truncate(TrebleClefUtf8), ", World!")));

  // Replicate a few other cases that were discovered during initial fuzzing,
  // to ensure we see these as invalid utf8 and avoid them in comparisons.
  EXPECT_FALSE(isValidUtf8("_K\301\234K", true));
  EXPECT_FALSE(isValidUtf8("\xF7\xA6\x8A\x8A", true));
  EXPECT_FALSE(isValidUtf8("\020\377\377\376\000", true));
}

} // namespace
} // namespace Json
} // namespace Envoy
