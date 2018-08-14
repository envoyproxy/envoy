#include "common/network/lc_trie.h"
#include "common/network/utility.h"

#include "testing/base/public/benchmark.h"

namespace {

std::vector<Envoy::Network::Address::InstanceConstSharedPtr> addresses;

std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>> tag_data;

std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>>
    tag_data_nested_prefixes;

std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>>
    tag_data_minimal;

std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie;

std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie_nested_prefixes;

std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie_minimal;

} // namespace

namespace Envoy {

static void BM_LcTrieConstruct(benchmark::State& state) {
  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(BM_LcTrieConstruct);

static void BM_LcTrieConstructNested(benchmark::State& state) {
  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data_nested_prefixes);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(BM_LcTrieConstructNested);

static void BM_LcTrieConstructMinimal(benchmark::State& state) {

  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data_minimal);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(BM_LcTrieConstructMinimal);

static void BM_LcTrieLookup(benchmark::State& state) {
  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    i++;
    i %= addresses.size();
    output_tags += lc_trie->getData(addresses[i]).size();
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
    output_tags += lc_trie_nested_prefixes->getData(addresses[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(BM_LcTrieLookupWithNestedPrefixes);

static void BM_LcTrieLookupMinimal(benchmark::State& state) {
  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    i++;
    i %= addresses.size();
    output_tags += lc_trie_minimal->getData(addresses[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(BM_LcTrieLookupMinimal);

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

  // Construct three sets of prefixes: one consisting of 1,024 addresses in an
  // RFC 5737 netblock, another consisting of those same addresses plus
  // 0.0.0.0/0 (to exercise the LC Trie's support for nested prefixes),
  // and finally a set containing only 0.0.0.0/0.
  for (int i = 0; i < 32; i++) {
    for (int j = 0; j < 32; j++) {
      tag_data.emplace_back(std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
          {"tag_1",
           {Envoy::Network::Address::CidrRange::create(fmt::format("192.0.{}.{}/32", i, j))}}));
    }
  }
  tag_data_nested_prefixes = tag_data;
  tag_data_nested_prefixes.emplace_back(
      std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
          {"tag_0", {Envoy::Network::Address::CidrRange::create("0.0.0.0/0")}}));
  tag_data_minimal.emplace_back(
      std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
          {"tag_1", {Envoy::Network::Address::CidrRange::create("0.0.0.0/0")}}));

  lc_trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data);
  lc_trie_nested_prefixes =
      std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data_nested_prefixes);
  lc_trie_minimal = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(tag_data_minimal);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
