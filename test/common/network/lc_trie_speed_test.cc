#include "common/network/lc_trie.h"
#include "common/network/utility.h"

#include "testing/base/public/benchmark.h"

namespace {

std::vector<Envoy::Network::Address::InstanceConstSharedPtr> addresses;

std::unique_ptr<Envoy::Network::LcTrie::LcTrie> lc_trie;

std::unique_ptr<Envoy::Network::LcTrie::LcTrie> lc_trie_nested_prefixes;

} // namespace

namespace Envoy {

static void BM_LcTrieLookup(benchmark::State& state) {
  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    i++;
    i %= addresses.size();
    output_tags += lc_trie->getTags(addresses[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(BM_LcTrieLookup);

static void BM_LcTrieLookupWithNestedPrefixes(benchmark::State& state) {
  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    i++;
    i %= addresses.size();
    output_tags += lc_trie_nested_prefixes->getTags(addresses[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(BM_LcTrieLookupWithNestedPrefixes);

} // namespace Envoy

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {

  // Random test addresses from RFC 5737 netblocks
  static const std::string test_addresses[] = {
      "192.0.2.225",   "198.51.100.55", "198.51.100.105", "192.0.2.150",   "203.0.113.162",
      "203.0.113.110", "203.0.113.99",  "198.51.100.23",  "198.51.100.24", "203.0.113.12"};
  for (const auto& address : test_addresses) {
    addresses.push_back(Envoy::Network::Utility::parseInternetAddress(address));
  }

  std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>> tag_data;
  for (int i = 0; i < 32; i++) {
    for (int j = 0; j < 32; j++) {
      tag_data.emplace_back(std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
          {"tag_1",
           {Envoy::Network::Address::CidrRange::create(fmt::format("192.0.{}.{}/32", i, j))}}));
    }
  }

  lc_trie = std::make_unique<Envoy::Network::LcTrie::LcTrie>(tag_data);
  tag_data.emplace_back(std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
      {"tag_0", {Envoy::Network::Address::CidrRange::create("0.0.0.0/0")}}));
  lc_trie_nested_prefixes = std::make_unique<Envoy::Network::LcTrie::LcTrie>(tag_data);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
