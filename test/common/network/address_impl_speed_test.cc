#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {
namespace Address {

static void ipv4InstanceCreate(benchmark::State& state) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443);
  static constexpr uint32_t Addr = 0xc00002ff; // From the RFC 5737 example range.
  addr.sin_addr.s_addr = htonl(Addr);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Ipv4Instance address(&addr);
    benchmark::DoNotOptimize(address.ip());
  }
}
BENCHMARK(ipv4InstanceCreate);

static void ipv6InstanceCreate(benchmark::State& state) {
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(443);
  static const char* Addr = "2001:DB8::1234"; // From the RFC 3849 example range.
  inet_pton(AF_INET6, Addr, &addr.sin6_addr);
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    Ipv6Instance address(addr);
    benchmark::DoNotOptimize(address.ip());
  }
}
BENCHMARK(ipv6InstanceCreate);

} // namespace Address
} // namespace Network
} // namespace Envoy
