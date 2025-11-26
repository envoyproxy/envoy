// Performance benchmark comparing LcTrie vs Linear Search for IP range matching
// in RBAC and access control scenarios.

#include <random>
#include <vector>

#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {
namespace Address {

// Generate realistic IP ranges for testing RBAC scenarios.
class IpRangeGenerator {
public:
  IpRangeGenerator() : generator_(42) {} // Fixed seed for reproducibility.

  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> generateIpv4Ranges(size_t count) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

    std::uniform_int_distribution<uint32_t> ip_dist(0, 0xFFFFFFFF);
    std::uniform_int_distribution<int> prefix_dist(8, 32); // Realistic CIDR prefixes

    for (size_t i = 0; i < count; ++i) {
      auto* range = ranges.Add();

      // Generate a random IPv4 address
      uint32_t ip = ip_dist(generator_);
      range->set_address_prefix(fmt::format("{}.{}.{}.{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                                            (ip >> 8) & 0xFF, ip & 0xFF));

      // Set a realistic prefix length
      range->mutable_prefix_len()->set_value(prefix_dist(generator_));
    }

    return ranges;
  }

  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> generateIpv6Ranges(size_t count) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

    std::uniform_int_distribution<uint16_t> segment_dist(0, 0xFFFF);
    std::uniform_int_distribution<int> prefix_dist(48, 128); // Realistic IPv6 prefixes

    for (size_t i = 0; i < count; ++i) {
      auto* range = ranges.Add();

      // Generate a random IPv6 address
      range->set_address_prefix(fmt::format(
          "{}:{}:{}:{}:{}:{}:{}:{}", segment_dist(generator_), segment_dist(generator_),
          segment_dist(generator_), segment_dist(generator_), segment_dist(generator_),
          segment_dist(generator_), segment_dist(generator_), segment_dist(generator_)));

      range->mutable_prefix_len()->set_value(prefix_dist(generator_));
    }

    return ranges;
  }

  std::vector<InstanceConstSharedPtr> generateTestIps(size_t count, bool ipv6 = false) {
    std::vector<InstanceConstSharedPtr> ips;
    ips.reserve(count);

    if (ipv6) {
      std::uniform_int_distribution<uint16_t> segment_dist(0, 0xFFFF);
      for (size_t i = 0; i < count; ++i) {
        const std::string ip_str = fmt::format(
            "{}:{}:{}:{}:{}:{}:{}:{}", segment_dist(generator_), segment_dist(generator_),
            segment_dist(generator_), segment_dist(generator_), segment_dist(generator_),
            segment_dist(generator_), segment_dist(generator_), segment_dist(generator_));
        auto ip = Utility::parseInternetAddressNoThrow(ip_str);
        if (ip) {
          ips.push_back(ip);
        }
      }
    } else {
      std::uniform_int_distribution<uint32_t> ip_dist(0, 0xFFFFFFFF);
      for (size_t i = 0; i < count; ++i) {
        uint32_t ip = ip_dist(generator_);
        const std::string ip_str = fmt::format("{}.{}.{}.{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                                               (ip >> 8) & 0xFF, ip & 0xFF);
        auto parsed_ip = Utility::parseInternetAddressNoThrow(ip_str);
        if (parsed_ip) {
          ips.push_back(parsed_ip);
        }
      }
    }

    return ips;
  }

private:
  std::mt19937 generator_;
};

// Helper function to convert protobuf ranges to CidrRange vector.
std::vector<CidrRange>
protobufToCidrRanges(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& ranges) {
  std::vector<CidrRange> cidr_ranges;
  cidr_ranges.reserve(ranges.size());
  for (const auto& range : ranges) {
    auto cidr_result = CidrRange::create(range);
    if (cidr_result.ok()) {
      cidr_ranges.push_back(std::move(cidr_result.value()));
    }
  }
  return cidr_ranges;
}

// Benchmark linear search IP list implementation.
static void BM_LinearIpListMatching(benchmark::State& state) {
  const size_t num_ranges = state.range(0);
  const size_t num_queries = 1000;

  IpRangeGenerator generator;
  auto ranges = generator.generateIpv4Ranges(num_ranges);
  auto test_ips = generator.generateTestIps(num_queries);

  auto ip_list_result = IpList::create(ranges);
  if (!ip_list_result.ok()) {
    state.SkipWithError("Failed to create IpList");
    return;
  }
  auto ip_list = std::move(ip_list_result.value());

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, test_ips.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 1024; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    const auto& query_ip = test_ips[query_indices[query_idx % 1024]];
    bool result = ip_list->contains(*query_ip);
    benchmark::DoNotOptimize(result);
    query_idx++;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("LinearSearch_{}ranges", num_ranges));
}

// Benchmark LcTrie IP list implementation using existing Network::LcTrie::LcTrie.
static void BM_LcTrieIpListMatching(benchmark::State& state) {
  const size_t num_ranges = state.range(0);
  const size_t num_queries = 1000;

  IpRangeGenerator generator;
  auto ranges = generator.generateIpv4Ranges(num_ranges);
  auto test_ips = generator.generateTestIps(num_queries);

  // Convert protobuf ranges to CidrRange vector.
  auto cidr_ranges = protobufToCidrRanges(ranges);
  if (cidr_ranges.empty()) {
    state.SkipWithError("Failed to convert ranges to CidrRange");
    return;
  }

  // Create LC Trie directly following the pattern from Unified IP Matcher.
  Network::LcTrie::LcTrie<bool> trie(
      std::vector<std::pair<bool, std::vector<CidrRange>>>{{true, cidr_ranges}});

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, test_ips.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 1024; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    const auto& query_ip = test_ips[query_indices[query_idx % 1024]];
    bool result = !trie.getData(query_ip).empty();
    benchmark::DoNotOptimize(result);
    query_idx++;
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("LcTrie_{}ranges", num_ranges));
}

// IPv6 benchmarks
static void BM_LinearIpListMatchingIPv6(benchmark::State& state) {
  const size_t num_ranges = state.range(0);
  const size_t num_queries = 1000;

  IpRangeGenerator generator;
  auto ranges = generator.generateIpv6Ranges(num_ranges);
  auto test_ips = generator.generateTestIps(num_queries, true);

  auto ip_list_result = IpList::create(ranges);
  if (!ip_list_result.ok()) {
    state.SkipWithError("Failed to create IpList");
    return;
  }
  auto ip_list = std::move(ip_list_result.value());

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, test_ips.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 512; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    if (query_idx < query_indices.size()) {
      const auto& query_ip = test_ips[query_indices[query_idx % 512]];
      bool result = ip_list->contains(*query_ip);
      benchmark::DoNotOptimize(result);
      query_idx++;
    }
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("LinearSearch_IPv6_{}ranges", num_ranges));
}

static void BM_LcTrieIpListMatchingIPv6(benchmark::State& state) {
  const size_t num_ranges = state.range(0);
  const size_t num_queries = 1000;

  IpRangeGenerator generator;
  auto ranges = generator.generateIpv6Ranges(num_ranges);
  auto test_ips = generator.generateTestIps(num_queries, true);

  // Convert protobuf ranges to CidrRange vector.
  auto cidr_ranges = protobufToCidrRanges(ranges);
  if (cidr_ranges.empty()) {
    state.SkipWithError("Failed to convert ranges to CidrRange");
    return;
  }

  // Create LC Trie directly following the pattern from Unified IP Matcher.
  Network::LcTrie::LcTrie<bool> trie(
      std::vector<std::pair<bool, std::vector<CidrRange>>>{{true, cidr_ranges}});

  // Pre-generate random queries for consistent benchmark.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<size_t> dist(0, test_ips.size() - 1);
  std::vector<size_t> query_indices;
  for (size_t i = 0; i < 512; ++i) {
    query_indices.push_back(dist(rng));
  }

  size_t query_idx = 0;
  for (auto _ : state) {
    if (query_idx < query_indices.size()) {
      const auto& query_ip = test_ips[query_indices[query_idx % 512]];
      bool result = !trie.getData(query_ip).empty();
      benchmark::DoNotOptimize(result);
      query_idx++;
    }
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel(fmt::format("LcTrie_IPv6_{}ranges", num_ranges));
}

// Comprehensive benchmarks for RBAC scenarios
BENCHMARK(BM_LinearIpListMatching)->Range(10, 5000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_LcTrieIpListMatching)->Range(10, 5000)->Unit(benchmark::kNanosecond);

// Focused benchmarks for common RBAC policy sizes
BENCHMARK(BM_LinearIpListMatching)->Arg(25)->Arg(50)->Arg(100)->Arg(250)->Arg(500)->Arg(1000);
BENCHMARK(BM_LcTrieIpListMatching)->Arg(25)->Arg(50)->Arg(100)->Arg(250)->Arg(500)->Arg(1000);

// IPv6 benchmarks for realistic dual-stack scenarios
BENCHMARK(BM_LinearIpListMatchingIPv6)->Arg(50)->Arg(200)->Arg(500);
BENCHMARK(BM_LcTrieIpListMatchingIPv6)->Arg(50)->Arg(200)->Arg(500);

} // namespace Address
} // namespace Network
} // namespace Envoy
