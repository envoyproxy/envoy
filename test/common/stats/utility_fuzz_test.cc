#include "common/stats/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const absl::string_view string_buffer(reinterpret_cast<const char*>(buf), len);
  Stats::Utility::sanitizeStatsName(string_buffer);
}

} // namespace Fuzz
} // namespace Envoy
