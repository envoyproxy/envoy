#include "common/network/lc_trie.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

template <class IpType, uint32_t address_size>
const uint32_t LcTrie::LcTrieInternal<IpType, address_size>::MAXIMUM_CIDR_RANGE_ENTRIES;

LcTrie::LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
               double fill_factor, uint32_t root_branching_factor) {
  std::vector<IpPrefix<Ipv4>> ipv4_prefixes;
  std::vector<IpPrefix<Ipv6>> ipv6_prefixes;
  for (const auto& pair_data : tag_data) {
    for (const auto& cidr_range : pair_data.second) {
      if (cidr_range.ip()->version() == Address::IpVersion::v4) {
        IpPrefix<Ipv4> ip_prefix;
        ip_prefix.tag_ = pair_data.first;
        ip_prefix.length_ = cidr_range.length();
        ip_prefix.ip_ = ntohl(cidr_range.ip()->ipv4()->address());
        ipv4_prefixes.push_back(ip_prefix);
      } else {
        IpPrefix<Ipv6> ip_prefix;
        ip_prefix.tag_ = pair_data.first;
        ip_prefix.length_ = cidr_range.length();
        ip_prefix.ip_ = Utility::Ip6ntohl(cidr_range.ip()->ipv6()->address());
        ipv6_prefixes.push_back(ip_prefix);
      }
    }
  }
  ipv4_trie_.reset(new LcTrieInternal<Ipv4>(ipv4_prefixes, fill_factor, root_branching_factor));
  ipv6_trie_.reset(new LcTrieInternal<Ipv6>(ipv6_prefixes, fill_factor, root_branching_factor));
}

std::string LcTrie::getTag(const Network::Address::InstanceConstSharedPtr& ip_address) const {
  if (ip_address->ip()->version() == Address::IpVersion::v4) {
    Ipv4 ip = ntohl(ip_address->ip()->ipv4()->address());
    return ipv4_trie_->getTag(ip);
  } else {
    Ipv6 ip = Utility::Ip6ntohl(ip_address->ip()->ipv6()->address());
    return ipv6_trie_->getTag(ip);
  }
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
