#include "source/common/network/address_impl.h"
#include "source/extensions/network/socket_interface/sockmap/bpf_datapath.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {
namespace {

// Building the sockhash key is the extra CPU work the sockmap datapath adds in Envoy per
// accelerated socket, paid once on the first transfer of a same-host hop before the one-time map
// update syscall.
void benchmarkBuildSockKeyIpv4(::benchmark::State& state) {
  Address::Ipv4Instance local("10.0.0.1", 1111);
  Address::Ipv4Instance peer("10.0.0.2", 2222);
  SockKey key;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    ::benchmark::DoNotOptimize(buildSockKey(local, peer, key));
    ::benchmark::DoNotOptimize(key);
  }
}
BENCHMARK(benchmarkBuildSockKeyIpv4);

// A non IPv4 socket is rejected before any key is built and then falls back to TCP/IP, so this
// measures the cost a non accelerated socket pays before taking the standard datapath.
void benchmarkBuildSockKeyIpv6Reject(::benchmark::State& state) {
  Address::Ipv6Instance local("::1", 1111);
  Address::Ipv6Instance peer("::2", 2222);
  SockKey key;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    ::benchmark::DoNotOptimize(buildSockKey(local, peer, key));
  }
}
BENCHMARK(benchmarkBuildSockKeyIpv6Reject);

} // namespace
} // namespace Network
} // namespace Envoy
