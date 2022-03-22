#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

const Envoy::Json::JsonSanitizer& static_sanitizer() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Json::JsonSanitizer);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const Envoy::Json::JsonSanitizer& sanitizer = static_sanitizer();
  FuzzedDataProvider provider(buf, len);
  std::string buffer;
  while (provider.remaining_bytes() != 0) {
    std::string input = provider.ConsumeRandomLengthString(provider.remaining_bytes());

    // to differentially fuzz-test JsonSanitizer against protobuf serialization,
    // we must ensure the string is valid UTF-8, otherwise protobuf serialization
    // fails with an error message that would prevent effective fuzzing. So we
    // mutate the high order bits of the string as needed to make sure all it is
    // valid utf-8. See https://en.wikipedia.org/wiki/UTF-8.
    for (uint32_t i = 0; i < input.size(); ++i) {
      uint8_t& byte1 = reinterpret_cast<uint8_t&>(input[i]);
      const uint8_t high_order_bit = 0x80; // 1000 0000

      if ((byte1 & high_order_bit) != 0) {
        if (i == input.size() - 1) {
          // If this is the last byte, then zero out the high order bit, as it
          // can't be part of a 2 byte sequence; just zero out the high-order
          // bit, as single-byte patterns with the high order clear set are
          // valid utf-8, mapping exactly to 7-bit ascii.
          byte1 &= 0x7f;
        } else {
          // Make sure byte1 has high order 3 bits 110.
          const uint8_t utf8_byte1_lower_mask = 0xDF;    // 0001 1111
          const uint8_t utf8_byte1_upper_pattern = 0xC0; // 1100 0000
          byte1 = (byte1 & utf8_byte1_lower_mask) | utf8_byte1_upper_pattern;

          // Make sure byte1 has high order 2 bits 10.
          const uint8_t utf8_byte2_lower_mask = 0x3F;    // 0011 1111
          const uint8_t utf8_byte2_upper_pattern = 0x80; // 1000 0000
          ++i;
          uint8_t& byte2 = reinterpret_cast<uint8_t&>(input[i]);
          byte2 = (byte2 & utf8_byte2_lower_mask) | utf8_byte2_upper_pattern;
        }
      }
    }

    absl::string_view hand_sanitized = sanitizer.sanitize(buffer, input);
    std::string proto_sanitized =
        MessageUtil::getJsonStringFromMessageOrDie(ValueUtil::stringValue(input), false, true);
    FUZZ_ASSERT(proto_sanitized.size() >= 2);
    FUZZ_ASSERT(proto_sanitized[0] == '"');
    FUZZ_ASSERT(proto_sanitized[proto_sanitized.size() - 1] == '"');
    auto proto_view = absl::string_view(proto_sanitized).substr(1, proto_sanitized.size() - 2);
    if (hand_sanitized != proto_view) {
      ENVOY_LOG_MISC(error, "{} != {}", hand_sanitized != proto_view);
    }
    FUZZ_ASSERT(hand_sanitized == proto_view);
  }
}

} // namespace Fuzz
} // namespace Envoy
