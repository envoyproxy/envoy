#include "test/fuzz/fuzz_runner.h"
#include "test/fuzzing_codelab/date_formatter.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string input(reinterpret_cast<const char*>(buf), len);
  const SystemTime time{std::chrono::seconds(0)};
  DateFormatter(input).fromTime(time);
}

} // namespace Fuzz
} // namespace Envoy
