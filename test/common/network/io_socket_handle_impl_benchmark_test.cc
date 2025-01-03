#include <memory>

#include "source/common/network/io_socket_handle_impl.h"

#include "test/test_common/network_utility.h"

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {
namespace Test {

std::vector<sockaddr_storage> getSockAddrSampleAddresses(const int count) {
  std::vector<sockaddr_storage> addresses;
  for (int i = 0; i < count; i += 4) {
    int ip_suffix = 101 + i;
    // A sample v6 source address.
    addresses.push_back(getV6SockAddr(absl::StrCat("2001:DB8::", ip_suffix), 51234));
    // A sample v6 destination address.
    addresses.push_back(getV6SockAddr(absl::StrCat("2001:DB8::", ip_suffix), 443));
    // A sample v4 source address.
    addresses.push_back(getV4SockAddr(absl::StrCat("203.0.113.", ip_suffix), 52345));
    // A sample v4 destination address.
    addresses.push_back(getV4SockAddr(absl::StrCat("203.0.113.", ip_suffix), 443));
  }
  return addresses;
}

} // namespace Test

class IoSocketHandleImplTestWrapper {
public:
  explicit IoSocketHandleImplTestWrapper(const int cache_size)
      : io_handle_(-1, false, absl::nullopt, cache_size) {}

  Address::InstanceConstSharedPtr getOrCreateEnvoyAddressInstances(const sockaddr_storage& ss) {
    return io_handle_.getOrCreateEnvoyAddressInstance(ss, Test::getSockAddrLen(ss));
  }

private:
  IoSocketHandleImpl io_handle_;
};

static void BM_GetOrCreateEnvoyAddressInstanceNoCache(benchmark::State& state) {
  std::vector<sockaddr_storage> addresses = Test::getSockAddrSampleAddresses(/*count=*/4);
  IoSocketHandleImplTestWrapper wrapper(/*cache_size=*/0);
  for (auto _ : state) {
    for (int i = 0; i < 50; ++i) {
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(addresses[0]));
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(addresses[1]));
    }
  }
}
BENCHMARK(BM_GetOrCreateEnvoyAddressInstanceNoCache)->Iterations(1000);

static void BM_GetOrCreateEnvoyAddressInstanceConnectedSocket(benchmark::State& state) {
  std::vector<sockaddr_storage> addresses = Test::getSockAddrSampleAddresses(/*count=*/4);
  IoSocketHandleImplTestWrapper wrapper(/*cache_size=*/4);
  for (auto _ : state) {
    for (int i = 0; i < 50; ++i) {
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(addresses[0]));
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(addresses[1]));
    }
  }
}
BENCHMARK(BM_GetOrCreateEnvoyAddressInstanceConnectedSocket)->Iterations(1000);

static void BM_GetOrCreateEnvoyAddressInstanceUnconnectedSocket(benchmark::State& state) {
  std::vector<sockaddr_storage> addresses = Test::getSockAddrSampleAddresses(/*count=*/100);
  IoSocketHandleImplTestWrapper wrapper(/*cache_size=*/4);
  for (auto _ : state) {
    for (const sockaddr_storage& ss : addresses) {
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(ss));
    }
  }
}
BENCHMARK(BM_GetOrCreateEnvoyAddressInstanceUnconnectedSocket)->Iterations(1000);

static void
BM_GetOrCreateEnvoyAddressInstanceUnconnectedSocketLargerCache(benchmark::State& state) {
  std::vector<sockaddr_storage> addresses = Test::getSockAddrSampleAddresses(/*count=*/100);
  IoSocketHandleImplTestWrapper wrapper(/*cache_size=*/50);
  for (auto _ : state) {
    for (const sockaddr_storage& ss : addresses) {
      benchmark::DoNotOptimize(wrapper.getOrCreateEnvoyAddressInstances(ss));
    }
  }
}
BENCHMARK(BM_GetOrCreateEnvoyAddressInstanceUnconnectedSocketLargerCache)->Iterations(1000);

} // namespace Network
} // namespace Envoy
