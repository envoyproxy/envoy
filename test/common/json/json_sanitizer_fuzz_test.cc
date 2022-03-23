#include "source/common/json/json_sanitizer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/json_sanitizer_test_util.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Fuzz {

const Envoy::Json::JsonSanitizer& staticSanitizer() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Json::JsonSanitizer);
}

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const Envoy::Json::JsonSanitizer& sanitizer = staticSanitizer();
  FuzzedDataProvider provider(buf, len);
  std::string buffer1, buffer2;
  while (provider.remaining_bytes() != 0) {
    std::string input = provider.ConsumeRandomLengthString(provider.remaining_bytes());
    absl::string_view hand_sanitized = sanitizer.sanitize(buffer1, input);

    // If the input is valid UTF-8 we can do a differential test against the
    // Protobuf JSON sanitizer. Otherwise we are simply ensuring that the
    // sanitizer does not crash.
    if (Envoy::Json::JsonSanitizer::isValidUtf8(input)) {
      buffer2 =
          MessageUtil::getJsonStringFromMessageOrDie(ValueUtil::stringValue(input), false, true);
      absl::string_view proto_sanitized = Envoy::Json::stripDoubleQuotes(buffer2);
      if (hand_sanitized != proto_sanitized) {
        std::cerr << hand_sanitized << " != " << proto_sanitized << std::endl;
      }
      FUZZ_ASSERT(hand_sanitized == proto_sanitized);
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
