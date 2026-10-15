// Measures the cost of sorting an upstream address list with address families
// interleaved as per RFC 8305 (happy eyeballs v2). This sort used to run on
// every upstream connection attempt; it now runs once when a host's address
// list is created or refreshed.

#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/happy_eyeballs_connection_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {

static void happyEyeballsSortAddresses(benchmark::State& state) {
  const uint64_t num_addresses = state.range(0);
  std::vector<Address::InstanceConstSharedPtr> address_list;
  address_list.reserve(num_addresses);
  // Build a dual family list interleaving IPv6 and IPv4 addresses.
  for (uint64_t i = 0; i < num_addresses; i++) {
    if (i % 2 == 0) {
      address_list.push_back(
          std::make_shared<Address::Ipv6Instance>(fmt::format("2001:db8::{}", i + 1), 443));
    } else {
      address_list.push_back(
          std::make_shared<Address::Ipv4Instance>(fmt::format("10.0.0.{}", i + 1)));
    }
  }
  envoy::config::cluster::v3::UpstreamConnectionOptions::HappyEyeballsConfig config;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(HappyEyeballsConnectionProvider::sortAddresses(address_list, config));
  }
}
BENCHMARK(happyEyeballsSortAddresses)->Arg(2)->Arg(8)->Arg(16);

} // namespace Network
} // namespace Envoy
