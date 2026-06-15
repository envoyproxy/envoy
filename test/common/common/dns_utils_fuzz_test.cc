#include "source/common/common/dns_utils.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  std::vector<uint8_t> rdata(buf, buf + len);
  DnsUtils::parseHttpsRecord(rdata);
}

} // namespace Fuzz
} // namespace Envoy
