#include <memory>

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
             bool exclusive = false, double fill_factor = 0, uint32_t root_branch_factor = 0) {
    std::vector<std::pair<std::string, std::vector<Address::CidrRange>>> output;
    for (size_t i = 0; i < cidr_range_strings.size(); i++) {
      std::pair<std::string, std::vector<Address::CidrRange>> ip_tags;
      ip_tags.first = fmt::format("tag_{0}", i);
      for (const auto& j : cidr_range_strings[i]) {
        ip_tags.second.push_back(Address::CidrRange::create(j));
      }
      output.push_back(ip_tags);
    }
    // Use custom fill factors and root branch factors if they are in the valid range.
    if ((fill_factor > 0) && (fill_factor <= 1) && (root_branch_factor > 0)) {
      trie_ =
          std::make_unique<LcTrie<std::string>>(output, exclusive, fill_factor, root_branch_factor);
    } else {
      trie_ = std::make_unique<LcTrie<std::string>>(output, exclusive);
    }
  }

  void expectIPAndTags(
      const std::vector<std::pair<std::string, std::vector<std::string>>>& test_output) {
    for (const auto& kv : test_output) {
      std::vector<std::string> expected(kv.second);
      std::sort(expected.begin(), expected.end());
      std::vector<std::string> actual(trie_->getData(Utility::parseInternetAddress(kv.first)));
      std::sort(actual.begin(), actual.end());
      EXPECT_EQ(expected, actual);
    }
  }

  std::unique_ptr<LcTrie<std::string>> trie_;
};

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
  setup(cidr_range_strings, false, fill_factor, root_branching_factor);

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
  const std::vector<std::vector<std::string>> cidr_range_strings = {
      {"203.0.113.0/24", "203.0.113.128/25"}, // tag_0
      {"203.0.113.255/32"},                   // tag_1
      {"198.51.100.0/24"},                    // tag_2
      {"2001:db8::/96", "2001:db8::8000/97"}, // tag_3
      {"2001:db8::ffff/128"},                 // tag_4
      {"2001:db8:1::/48"},                    // tag_5
      {"2001:db8:1::/128", "2001:db8:1::/48"} // tag_6
  };
  setup(cidr_range_strings);

  const std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"203.0.113.0", {"tag_0"}},
      {"203.0.113.192", {"tag_0"}},
      {"203.0.113.255", {"tag_0", "tag_1"}},
      {"198.51.100.1", {"tag_2"}},
      {"2001:db8::ffff", {"tag_3", "tag_4"}},
      {"2001:db8:1::ffff", {"tag_5", "tag_6"}}};
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, NestedPrefixesWithCatchAll) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"0.0.0.0/0"},                          // tag_0
      {"203.0.113.0/24"},                     // tag_1
      {"203.0.113.128/25"},                   // tag_2
      {"198.51.100.0/24"},                    // tag_3
      {"::0/0"},                              // tag_4
      {"2001:db8::/96", "2001:db8::8000/97"}, // tag_5
      {"2001:db8::ffff/128"},                 // tag_6
      {"2001:db8:1::/48"},                    // tag_7
      {"203.0.113.0/24"}                      // tag_8 (same subnet as tag_1)
  };
  setup(cidr_range_strings);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"203.0.0.0", {"tag_0"}},
      {"203.0.113.0", {"tag_0", "tag_1", "tag_8"}},
      {"203.0.113.192", {"tag_0", "tag_1", "tag_2", "tag_8"}},
      {"203.0.113.255", {"tag_0", "tag_1", "tag_2", "tag_8"}},
      {"198.51.100.1", {"tag_0", "tag_3"}},
      {"2001:db8::ffff", {"tag_4", "tag_5", "tag_6"}},
      {"2001:db8:1::ffff", {"tag_4", "tag_7"}}};
  expectIPAndTags(test_case);
}

TEST_F(LcTrieTest, ExclusiveNestedPrefixesWithCatchAll) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"0.0.0.0/0"},                          // tag_0
      {"203.0.113.0/24"},                     // tag_1
      {"203.0.113.128/25"},                   // tag_2
      {"198.51.100.0/24"},                    // tag_3
      {"::0/0"},                              // tag_4
      {"2001:db8::/96", "2001:db8::8000/97"}, // tag_5
      {"2001:db8::ffff/128"},                 // tag_6
      {"2001:db8:1::/48"},                    // tag_7
      {"203.0.113.0/24"}                      // tag_8 (same subnet as tag_1)
  };
  setup(cidr_range_strings, true);

  std::vector<std::pair<std::string, std::vector<std::string>>> test_case = {
      {"203.0.0.0", {"tag_0"}},       {"203.0.113.0", {"tag_1", "tag_8"}},
      {"203.0.113.192", {"tag_2"}},   {"203.0.113.255", {"tag_2"}},
      {"198.51.100.1", {"tag_3"}},    {"2001:db8::ffff", {"tag_6"}},
      {"2001:db8:1::ffff", {"tag_7"}}};
  expectIPAndTags(test_case);
}

// Ensure the trie will reject inputs that would cause it to exceed the maximum 2^20 nodes
// when using the default fill factor.
TEST_F(LcTrieTest, MaximumEntriesExceptionDefault) {
  static const size_t num_prefixes = 1 << 19;
  Address::CidrRange address = Address::CidrRange::create("10.0.0.1/8");
  std::vector<Address::CidrRange> prefixes;
  prefixes.reserve(num_prefixes);
  for (size_t i = 0; i < num_prefixes; i++) {
    prefixes.push_back(address);
  }
  EXPECT_EQ(num_prefixes, prefixes.size());

  std::pair<std::string, std::vector<Address::CidrRange>> ip_tag =
      std::make_pair("bad_tag", prefixes);
  std::vector<std::pair<std::string, std::vector<Address::CidrRange>>> ip_tags_input{ip_tag};
  EXPECT_THROW_WITH_MESSAGE(new LcTrie<std::string>(ip_tags_input), EnvoyException,
                            "The input vector has '524288' CIDR range entries. "
                            "LC-Trie can only support '262144' CIDR ranges with "
                            "the specified fill factor.");
}

// Ensure the trie will reject inputs that would cause it to exceed the maximum 2^20 nodes
// when using a fill factor override.
TEST_F(LcTrieTest, MaximumEntriesExceptionOverride) {
  static const size_t num_prefixes = 8192;
  std::vector<Address::CidrRange> prefixes;
  prefixes.reserve(num_prefixes);
  for (size_t i = 0; i < 16; i++) {
    for (size_t j = 0; j < 16; j++) {
      for (size_t k = 0; k < 32; k++) {
        prefixes.emplace_back(Address::CidrRange::create(fmt::format("10.{}.{}.{}/8", i, j, k)));
      }
    }
  }
  EXPECT_EQ(num_prefixes, prefixes.size());

  std::pair<std::string, std::vector<Address::CidrRange>> ip_tag =
      std::make_pair("bad_tag", prefixes);
  std::vector<std::pair<std::string, std::vector<Address::CidrRange>>> ip_tags_input{ip_tag};
  EXPECT_THROW_WITH_MESSAGE(new LcTrie<std::string>(ip_tags_input, false, 0.01), EnvoyException,
                            "The input vector has '8192' CIDR range entries. "
                            "LC-Trie can only support '5242' CIDR ranges with "
                            "the specified fill factor.");
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
