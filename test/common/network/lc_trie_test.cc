#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

class LcTrieTest : public testing::Test {
public:
  void setup(const std::vector<std::vector<std::string>>& cidr_range_strings,
             double fill_factor = 0, uint32_t root_branch_factor = 0) {
    std::vector<std::pair<std::string, std::vector<Address::CidrRange>>> output;
    for (size_t i = 0; i < cidr_range_strings.size(); i++) {
      std::pair<std::string, std::vector<Address::CidrRange>> ip_tags;
      ip_tags.first = fmt::format("tag_{0}", i);
      for (size_t j = 0; j < cidr_range_strings[i].size(); j++) {
        ip_tags.second.push_back(Address::CidrRange::create(cidr_range_strings[i][j]));
      }
      output.push_back(ip_tags);
    }
    // Use custom fill factors and root branch factors if they are in the valid range.
    if ((fill_factor > 0) && (fill_factor <= 1) && (root_branch_factor > 0)) {
      trie_.reset(new LcTrie(output, fill_factor, root_branch_factor));
    } else {
      trie_.reset(new LcTrie(output));
    }
  }

  void expectIPAndTags(
      const std::vector<std::pair<std::string, std::vector<std::string>>>& test_output) {
    for (const auto& kv : test_output) {
      EXPECT_EQ(kv.second, trie_->getTags(Utility::parseInternetAddress(kv.first)));
    }
  }

  std::unique_ptr<LcTrie> trie_;
};

// TODO(ccaraman): Add a performance and memory benchmark test.

// Use the default constructor values.
TEST_F(LcTrieTest, IPv4Defaults) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"0.0.0.0/4"},   // tag_0
      {"16.0.0.0/4"},  // tag_1
      {"40.0.0.0/5"},  // tag_2
      {"64.0.0.0/3"},  // tag_3
      {"96.0.0.0/4"},  // tag_4
      {"112.0.0.0/4"}, // tag_5
      {"128.0.0.0/3"}, // tag_6
      {"160.0.0.0/6"}, // tag_7
      {"164.0.0.0/6"}, // tag_8
      {"168.0.0.0/5"}, // tag_9
      {"176.0.0.0/5"}, // tag_10
      {"184.0.0.0/5"}, // tag_11
      {"192.0.0.0/3"}, // tag_12
      {"232.0.0.0/8"}, // tag_13
      {"233.0.0.0/8"}, // tag_14
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"0.0.0.0", {"tag_0"}},     {"16.0.0.1", {"tag_1"}},
      {"40.0.0.255", {"tag_2"}},  {"64.0.130.0", {"tag_3"}},
      {"96.0.0.10", {"tag_4"}},   {"112.0.0.0", {"tag_5"}},
      {"128.0.0.1", {"tag_6"}},   {"160.0.0.1", {"tag_7"}},
      {"164.255.0.0", {"tag_8"}}, {"168.0.0.0", {"tag_9"}},
      {"176.0.0.1", {"tag_10"}},  {"184.0.0.1", {"tag_11"}},
      {"192.0.0.0", {"tag_12"}},  {"232.0.80.0", {"tag_13"}},
      {"233.0.0.1", {"tag_14"}},  {"::1", {}},
  };
  expectIPAndTags(test_case);
}

// There was a bug in the C++ port that didn't update the index for the next address in the trie.
// For the data set below, the address "164.255.0.0" returned no tag instead of "tag_8".
TEST_F(LcTrieTest, RootBranchingFactor) {
  double fill_factor = 0.75;
  uint32_t root_branching_factor = 16;
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"0.0.0.0/4"},   // tag_0
      {"16.0.0.0/4"},  // tag_1
      {"40.0.0.0/5"},  // tag_2
      {"64.0.0.0/3"},  // tag_3
      {"96.0.0.0/4"},  // tag_4
      {"112.0.0.0/4"}, // tag_5
      {"128.0.0.0/3"}, // tag_6
      {"160.0.0.0/6"}, // tag_7
      {"164.0.0.0/6"}, // tag_8
      {"168.0.0.0/5"}, // tag_9
      {"176.0.0.0/5"}, // tag_10
      {"184.0.0.0/5"}, // tag_11
      {"192.0.0.0/3"}, // tag_12
      {"232.0.0.0/8"}, // tag_13
      {"233.0.0.0/8"}, // tag_14
  };
  setup(cidr_range_strings, fill_factor, root_branching_factor);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"0.0.0.0", {"tag_0"}},     {"16.0.0.1", {"tag_1"}},
      {"40.0.0.255", {"tag_2"}},  {"64.0.130.0", {"tag_3"}},
      {"96.0.0.10", {"tag_4"}},   {"112.0.0.0", {"tag_5"}},
      {"128.0.0.1", {"tag_6"}},   {"160.0.0.1", {"tag_7"}},
      {"164.255.0.0", {"tag_8"}}, {"168.0.0.0", {"tag_9"}},
      {"176.0.0.1", {"tag_10"}},  {"184.0.0.1", {"tag_11"}},
      {"192.0.0.0", {"tag_12"}},  {"232.0.80.0", {"tag_13"}},
      {"233.0.0.1", {"tag_14"}},  {"::1", {}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, IPv4AddressSizeBoundaries) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"1.2.3.4/24", "10.255.255.255/32"},                           // tag_0
      {"54.233.128.0/17", "205.251.192.100/26", "52.220.191.10/30"}, // tag_1
      {"10.255.255.254/32"}                                          // tag_2
  };

  setup(cidr_range_strings);
  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"205.251.192.100", {"tag_1"}},
      {"10.255.255.255", {"tag_0"}},
      {"52.220.191.10", {"tag_1"}},
      {"10.255.255.254", {"tag_2"}},
      {"18.232.0.255", {}}};
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, IPv4Boundaries) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"0.0.0.0/1"},                                 // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"}, // tag_1
      {"128.0.0.0/1"},                               // tag_2
  };

  setup(cidr_range_strings);
  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"10.255.255.255", {"tag_0"}},
      {"205.251.192.100", {"tag_2"}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, IPv6) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"2406:da00:2000::/40", "::1/128"},            // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"}, // tag_1
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"2406:da00:2000::1", {"tag_0"}},
      {"2001:abcd:ef01:2345::1", {"tag_1"}},
      {"::1", {"tag_0"}},
      {"1.2.3.4", {}},
      {"2400:ffff:ff00::", {}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, IPv6AddressSizeBoundaries) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"2406:da00:2000::/40", "::1/128"},            // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"}, // tag_1
      {"::/128"},                                    // tag_2
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"::1", {"tag_0"}},
      {"2406:da00:2000::1", {"tag_0"}},
      {"2001:abcd:ef01:2345::1", {"tag_1"}},
      {"::", {"tag_2"}},
      {"::2", {}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, IPv6Boundaries) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"8000::/1"},   // tag_0
      {"1.2.3.4/24"}, // tag_1
      {"::/1"},       // tag_2
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"::1", {"tag_2"}},
      {"::2", {"tag_2"}},
      {"8000::1", {"tag_0"}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, CatchAllIPv4Prefix) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"1.2.3.4/0"},                                // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"} // tag_1
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"2001:abcd:ef01:2345::1", {"tag_1"}},
      {"1.2.3.4", {"tag_0"}},
      {"255.255.255.255", {"tag_0"}},
      {"2400:ffff:ff00::", {}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, CatchAllIPv6Prefix) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"::/0"},      // tag_0
      {"1.2.3.4/24"} // tag_1
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"2001:abcd:ef01:2345::1", {"tag_0"}},
      {"1.2.3.4", {"tag_1"}},
      {"abcd::343", {"tag_0"}},
      {"255.255.255.255", {}}};
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, BothIpvVersions) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"2406:da00:2000::/40", "::1/128"},                            // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"},                 // tag_1
      {"1.2.3.4/24", "10.255.255.255/32"},                           // tag_2
      {"54.233.128.0/17", "205.251.192.100/26", "52.220.191.10/30"}, // tag_3
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"205.251.192.100", {"tag_3"}},
      {"10.255.255.255", {"tag_2"}},
      {"52.220.191.10", {"tag_3"}},
      {"2406:da00:2000::1", {"tag_0"}},
      {"2001:abcd:ef01:2345::1", {"tag_1"}},
      {"::1", {"tag_0"}},
      {"18.232.0.255", {}},
      {"2400:ffff:ff00::", {}},
  };
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, NestedPrefixes) {
  EXPECT_THROW_WITH_MESSAGE(setup({{"1.2.3.130/24", "1.2.3.255/32"}}), EnvoyException,
                            "LcTrie does not support nested prefixes. '1.2.3.255/32' is a nested "
                            "prefix of '1.2.3.0/24'.");

  EXPECT_THROW_WITH_MESSAGE(
      setup({{"0.0.0.0/0", "1.2.3.254/32"}}), EnvoyException,
      "LcTrie does not support nested prefixes. '1.2.3.254/32' is a nested prefix of '0.0.0.0/0'.");

  EXPECT_THROW_WITH_MESSAGE(setup({{"2406:da00:2000::/40", "2406:da00:2000::1/100"}}),
                            EnvoyException,
                            "LcTrie does not support nested prefixes. '2406:da00:2000::/100' is a "
                            "nested prefix of '2406:da00:2000::/40'.");

  EXPECT_THROW_WITH_MESSAGE(setup({{"::/0", "2406:da00:2000::1/100"}}), EnvoyException,
                            "LcTrie does not support nested prefixes. '2406:da00:2000::/100' is a "
                            "nested prefix of '::/0'.");
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
