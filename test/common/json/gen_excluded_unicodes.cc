#include "source/common/protobuf/utility.h"

#include "test/common/json/utf8.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {
namespace {

// Collects Unicode values that cannot be handled by the protobuf JSON encoder.
// This is not needed for correct operation of the JSON sanitizer, but it is
// needed for comparing sanitization results against the proto serializer, and
// for differential fuzzing. We need to avoid comparing sanitization results for
// strings containing utf-8 sequences that protobufs cannot serialize.
//
// bazel -c opt run test/common/json:gen_excluded_unicodes |& grep -v 'contains invalid UTF-8'
//
// The grep pipe is essential as otherwise you will be buried in thousands of
// messages from the protobuf library that cannot otherwise be trapped. The
// "-c opt" is recommended because otherwise it takes almost a minute to run
// through all 4-byte UTF-8 sequences.
//
// Running this prints two initialization blocks for invalid byte code ranges,
// which can then be pasted into the InvalidUnicodeSet constructor in
// json_sanitizer_test_util.cc.
//
// The output of this command can then be pasted into the InvalidUnicodeSet
// constructor for json_sanitizer_test_util.cc. It should only be necessary
// to re-compute the invalid set if the protobuf library changes.
class InvalidUnicodeCollector {
public:
  /**
   * Collects a Unicode value that cannot be parsed as utf8 by the protobuf serializer.
   *
   * @param unicode the unicode value
   */
  void collect(uint32_t unicode) { invalid_.insert(unicode, unicode + 1); }

  /**
   * Emits the collection of invalid Unicode ranges to stdout.
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

bool isInvalidProtobufSerialization(const std::string& str) {
  // To work around https://github.com/protocolbuffers/protobuf/issues/9729
  // we must examine its output for each unicode. The protobuf library's
  // invalid outputs take two forms:
  //   ""            (empty quoted string, which happens on 3-byte utf8)
  //   \u1234\u5678  (two consecutive unicode escapes for a 4-byte utf8)
  return str == "\"\"" || str.size() > 8;
}

void allThreeByteUtf8() {
  std::string utf8("abc");
  InvalidUnicodeCollector invalid;

  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | Utf8::Pattern3Byte;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | Utf8::ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | Utf8::ContinuePattern;
        auto [unicode, consumed] = Utf8::decode(utf8);
        if (consumed == 3) {
          std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrError(
              ValueUtil::stringValue(utf8), false, true);
          if (isInvalidProtobufSerialization(proto_sanitized)) {
            invalid.collect(unicode);
          }
        } else {
          ASSERT(consumed == 0);
        }
      }
    }
  }

  invalid.emit("invalid_3byte_intervals_");
}

void allFourByteUtf8() {
  std::string utf8("abcd");
  InvalidUnicodeCollector invalid;

  for (uint32_t byte1 = 0; byte1 < 16; ++byte1) {
    utf8[0] = byte1 | Utf8::Pattern4Byte;
    for (uint32_t byte2 = 0; byte2 < 64; ++byte2) {
      utf8[1] = byte2 | Utf8::ContinuePattern;
      for (uint32_t byte3 = 0; byte3 < 64; ++byte3) {
        utf8[2] = byte3 | Utf8::ContinuePattern;
        for (uint32_t byte4 = 0; byte4 < 64; ++byte4) {
          utf8[3] = byte4 | Utf8::ContinuePattern;
          auto [unicode, consumed] = Utf8::decode(utf8);
          if (consumed == 4) {
            std::string proto_sanitized = MessageUtil::getJsonStringFromMessageOrError(
                ValueUtil::stringValue(utf8), false, true);
            if (isInvalidProtobufSerialization(proto_sanitized)) {
              invalid.collect(unicode);
            }
          } else {
            ASSERT(consumed == 0);
          }
        }
      }
    }
  }
  invalid.emit("invalid_4byte_intervals_");
}

} // namespace
} // namespace Json
} // namespace Envoy

int main() {
  Envoy::Json::allThreeByteUtf8();
  Envoy::Json::allFourByteUtf8();
  return 0;
}
