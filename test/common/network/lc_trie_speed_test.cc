#include "source/common/network/lc_trie.h"
#include "source/common/network/utility.h"

#include "benchmark/benchmark.h"

namespace {

struct AddressInputs {
  AddressInputs() {
    // Random test addresses from RFC 5737 netblocks
    static const std::string test_addresses[] = {
        "192.0.2.225",   "198.51.100.55", "198.51.100.105", "192.0.2.150",   "203.0.113.162",
        "203.0.113.110", "203.0.113.99",  "198.51.100.23",  "198.51.100.24", "203.0.113.12"};
    for (const auto& address : test_addresses) {
      addresses_.push_back(Envoy::Network::Utility::parseInternetAddress(address));
    }
  }

  std::vector<Envoy::Network::Address::InstanceConstSharedPtr> addresses_;
};

struct CidrInputs {
  CidrInputs() {
    // Construct three sets of prefixes: one consisting of 1,024 addresses in an
    // RFC 5737 netblock, another consisting of those same addresses plus
    // 0.0.0.0/0 (to exercise the LC Trie's support for nested prefixes),
    // and finally a set containing only 0.0.0.0/0.
    for (int i = 0; i < 32; i++) {
      for (int j = 0; j < 32; j++) {
        tag_data_.emplace_back(
            std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
                {"tag_1",
                 {Envoy::Network::Address::CidrRange::create(
                     fmt::format("192.0.{}.{}/32", i, j))}}));
      }
    }
    tag_data_nested_prefixes_ = tag_data_;
    tag_data_nested_prefixes_.emplace_back(
        std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
            {"tag_0", {Envoy::Network::Address::CidrRange::create("0.0.0.0/0")}}));
    tag_data_minimal_.emplace_back(
        std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>(
            {"tag_1", {Envoy::Network::Address::CidrRange::create("0.0.0.0/0")}}));
  }

  std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>> tag_data_;
  std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>>
      tag_data_nested_prefixes_;
  std::vector<std::pair<std::string, std::vector<Envoy::Network::Address::CidrRange>>>
      tag_data_minimal_;
};

} // namespace

namespace Envoy {

static void lcTrieConstruct(benchmark::State& state) {
  CidrInputs inputs;

  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(inputs.tag_data_);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(lcTrieConstruct);

static void lcTrieConstructNested(benchmark::State& state) {
  CidrInputs inputs;

  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(
        inputs.tag_data_nested_prefixes_);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(lcTrieConstructNested);

static void lcTrieConstructMinimal(benchmark::State& state) {
  CidrInputs inputs;

  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> trie;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    trie = std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(inputs.tag_data_minimal_);
  }
  benchmark::DoNotOptimize(trie);
}

BENCHMARK(lcTrieConstructMinimal);

static void lcTrieLookup(benchmark::State& state) {
  CidrInputs cidr_inputs;
  AddressInputs address_inputs;
  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie =
      std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(cidr_inputs.tag_data_);

  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    i++;
    i %= address_inputs.addresses_.size();
    output_tags += lc_trie->getData(address_inputs.addresses_[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(lcTrieLookup);

static void lcTrieLookupWithNestedPrefixes(benchmark::State& state) {
  CidrInputs cidr_inputs;
  AddressInputs address_inputs;
  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie_nested_prefixes =
      std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(
          cidr_inputs.tag_data_nested_prefixes_);

  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    i++;
    i %= address_inputs.addresses_.size();
    output_tags += lc_trie_nested_prefixes->getData(address_inputs.addresses_[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(lcTrieLookupWithNestedPrefixes);

static void lcTrieLookupMinimal(benchmark::State& state) {
  CidrInputs cidr_inputs;
  AddressInputs address_inputs;
  std::unique_ptr<Envoy::Network::LcTrie::LcTrie<std::string>> lc_trie_minimal =
      std::make_unique<Envoy::Network::LcTrie::LcTrie<std::string>>(cidr_inputs.tag_data_minimal_);

  static size_t i = 0;
  size_t output_tags = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    i++;
    i %= address_inputs.addresses_.size();
    output_tags += lc_trie_minimal->getData(address_inputs.addresses_[i]).size();
  }
  benchmark::DoNotOptimize(output_tags);
}

BENCHMARK(lcTrieLookupMinimal);

} // namespace Envoy
