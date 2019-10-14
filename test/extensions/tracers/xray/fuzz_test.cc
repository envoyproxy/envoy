#include "extensions/tracers/xray/util.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  absl::string_view sv(reinterpret_cast<const char*>(buf), len);
  wildcardMatch("*", sv);
  wildcardMatch("?", sv);
  wildcardMatch("*??", sv);
  wildcardMatch("??*", sv);
  wildcardMatch(sv, "hello, world");
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
