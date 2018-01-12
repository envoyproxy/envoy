#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace LcTrie {
typedef std::pair<std::string, std::vector<Address::CidrRange>> IpTagData;

namespace {

std::vector<IpTagData>
generateTestCase(const std::vector<std::vector<std::string>>& cidr_range_strings) {
  std::vector<std::pair<std::string, std::vector<Address::CidrRange>>> output;
  IpTagData ip_tags;
  for (size_t i = 0; i < cidr_range_strings.size(); i++) {
    ip_tags.first = fmt::format("tag_{0}", i);
    for (size_t j = 0; j < cidr_range_strings[i].size(); j++) {
      ip_tags.second.push_back(Address::CidrRange::create(cidr_range_strings[i][j]));
    }
    output.push_back(ip_tags);
    ip_tags.second.clear();
  }
  return output;
};
} // namespace

// TODO(ccaraman) Add a perf test.

TEST(LcTrie, Ipv4) {

  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"1.2.3.4/24", "10.255.255.255/32"},
      {"54.233.128.0/17", "205.251.192.100/26", "52.220.191.10/30"}};

  LcTrie trie(generateTestCase(cidr_range_strings));

  std::vector<std::pair<std::string, std::string>> test_case = {
      {"205.251.192.100", "tag_1"},
      {"18.232.0.255", ""},
      {"10.255.255.255", "tag_0"},
      {"52.220.191.10", "tag_1"},
  };

  for (const auto& kv : test_case) {
    EXPECT_EQ(kv.second, trie.search(Utility::parseInternetAddress(kv.first)));
  }

  // Check Ipv6 path doesn't crash when the ipv6 trie is empty.
  EXPECT_EQ("", trie.search(Utility::parseInternetAddress("::1")));
}

TEST(LcTrie, Ipv6) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"2406:da00:2000::/40", "::1/128"}, {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"}};
  LcTrie trie(generateTestCase(cidr_range_strings));

  std::vector<std::pair<std::string, std::string>> test_case = {{"2400:ffff:ff00::", ""},
                                                                {"2406:da00:2000::1", "tag_0"},
                                                                {"2001:abcd:ef01:2345::1", "tag_1"},
                                                                {"::1", "tag_0"}};

  for (const auto& kv : test_case) {
    EXPECT_EQ(kv.second, trie.search(Utility::parseInternetAddress(kv.first)));
  }

  // Check the Ipv4 path doesn't crash when the Ipv4 trie is empty.
  EXPECT_EQ("", trie.search(Utility::parseInternetAddress("1.2.3.4")));
}

TEST(LcTrie, BothIpvVersions) {
  std::vector<std::vector<std::string>> cidr_range_strings = {
      {"2406:da00:2000::/40", "::1/128"},                             // tag_0
      {"2001:abcd:ef01:2345:6789:abcd:ef01:234/64"},                  // tag_1
      {"1.2.3.4/24", "10.255.255.255/32"},                            // tag_2
      {"54.233.128.0/17", "205.251.192.100/26", "52.220.191.10/30"}}; // tag_3
  LcTrie trie(generateTestCase(cidr_range_strings));

  std::vector<std::pair<std::string, std::string>> test_case = {
      {"205.251.192.100", "tag_3"},        {"18.232.0.255", ""},     {"10.255.255.255", "tag_2"},
      {"52.220.191.10", "tag_3"},          {"2400:ffff:ff00::", ""}, {"2406:da00:2000::1", "tag_0"},
      {"2001:abcd:ef01:2345::1", "tag_1"}, {"::1", "tag_0"}};

  for (const auto& kv : test_case) {
    EXPECT_EQ(kv.second, trie.search(Utility::parseInternetAddress(kv.first)));
  }
}

TEST(LcTrie, NestedPrefixes) {
  std::vector<std::vector<std::string>> cidr_range_strings_ipv4 = {{"1.2.3.4/24", "1.2.3.255/32"}};

  EXPECT_THROW(LcTrie(generateTestCase(cidr_range_strings_ipv4)), EnvoyException);

  std::vector<std::vector<std::string>> cidr_range_strings_ipv6 = {
      {"2406:da00:2000::/40", "2406:da00:2000::1/100"}};

  EXPECT_THROW(LcTrie(generateTestCase(cidr_range_strings_ipv6)), EnvoyException);
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
