#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Logger::DelegatingLogSink::escapeLogLine(
      absl::string_view(reinterpret_cast<const char*>(buf), len));
}

} // namespace Fuzz
} // namespace Envoy
