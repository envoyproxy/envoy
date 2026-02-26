#include "source/common/tracing/tracing_validation.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  absl::string_view input(reinterpret_cast<const char*>(buf), len);
  Envoy::Tracing::isValidTraceParent(input);
  Envoy::Tracing::isValidTraceState(input);
  Envoy::Tracing::isValidBaggage(input);
}

} // namespace Fuzz
} // namespace Envoy
