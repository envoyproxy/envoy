#include "url/gurl.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string input(reinterpret_cast<const char*>(buf), len);
  GURL url(input);
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
