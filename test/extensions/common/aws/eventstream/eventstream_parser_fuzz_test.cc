#include "source/extensions/common/aws/eventstream/eventstream_parser.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

using namespace Extensions::Common::Aws::Eventstream;

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const absl::string_view input(reinterpret_cast<const char*>(buf), len);

  // Fuzz parseMessage with arbitrary input (single-pass API)
  auto parse_result = EventstreamParser::parseMessage(input);
  // Result may be error or incomplete - that's fine for fuzzing

  // If we got a valid message, verify bytes_consumed is reasonable
  if (parse_result.ok() && parse_result->message.has_value()) {
    // Try parsing remaining data
    if (parse_result->bytes_consumed < input.size()) {
      EventstreamParser::parseMessage(input.substr(parse_result->bytes_consumed));
    }
  }

  // Test with multiple potential messages in buffer
  if (len > 32) {
    // Try parsing from different offsets
    for (size_t offset = 0; offset < len && offset < 100; offset += 16) {
      EventstreamParser::parseMessage(input.substr(offset));
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
