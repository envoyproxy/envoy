#include "test/fuzz/fuzz_runner.h"

#include "url/gurl.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string input(reinterpret_cast<const char*>(buf), len);

  GURL url(input);
  url.is_valid();
  url.scheme();
  url.host();
  url.EffectiveIntPort();
  url.path();
  url.query();
  url.ref();
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
