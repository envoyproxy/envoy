#include "source/common/common/logger.h"
#include "source/extensions/tracers/xray/util.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

// TODO(@marcomagdy): @htuch suggests to compare results with re2 (after replacing * with .* and ?
// with '.' and doing proper regex escaping)
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  absl::string_view pattern, input;
  if (len > 1) {
    // The input to wildcardMatch usually is http_method hostname url
    // Limit the size of the input for all of these to 4 kBytes to reduce runtime when running
    // fuzzer with asan.
    constexpr size_t inputlimit = 4096;
    if (len <= inputlimit) {
      pattern = absl::string_view(reinterpret_cast<const char*>(buf), len / 2);
      input = absl::string_view(reinterpret_cast<const char*>(buf + len / 2), len - len / 2);
      wildcardMatch(pattern, input);
    } else {
      ENVOY_LOG_MISC(debug, "Input size of {} bytes exceeds limit of {} bytes, rejecting", len,
                     inputlimit);
      return;
    }
  } else { // buf is a single byte, use it for both pattern and input
    absl::string_view sv(reinterpret_cast<const char*>(buf), len);
    wildcardMatch(sv, "hello");
    wildcardMatch("*", sv);
    wildcardMatch("?", sv);
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
