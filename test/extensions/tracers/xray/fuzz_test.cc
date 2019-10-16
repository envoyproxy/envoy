#include "extensions/tracers/xray/util.h"

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
    pattern = absl::string_view(reinterpret_cast<const char*>(buf), len / 2);
    input = absl::string_view(reinterpret_cast<const char*>(buf + len / 2), len - len / 2);
    wildcardMatch(pattern, input);
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
