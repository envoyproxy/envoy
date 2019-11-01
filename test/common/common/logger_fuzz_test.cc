#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string string_buffer(reinterpret_cast<const char*>(buf), len);
  Logger::DelegatingLogSink::escapeLogLine(string_buffer);
}

} // namespace Fuzz
} // namespace Envoy
