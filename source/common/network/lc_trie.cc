#include "common/network/lc_trie.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

LcTrie::LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
               double fill_factor, uint32_t root_branching_factor) {

  std::vector<IpPrefixes<Ipv4>> ipv4_prefixes;
  std::vector<IpPrefixes<Ipv6>> ipv6_prefixes;
  for (const auto& pair_data : tag_data) {
    for (const auto& cidr_range : pair_data.second) {
      if (cidr_range.ip()->version() == Address::IpVersion::v4) {
        IpPrefixes<Ipv4> ip_prefix;
        ip_prefix.tag_ = pair_data.first;
        ip_prefix.length_ = cidr_range.length();
        ip_prefix.ip_ = ntohl(cidr_range.ip()->ipv4()->address());
        ipv4_prefixes.push_back(ip_prefix);
      } else {
        IpPrefixes<Ipv6> ip_prefix;
        ip_prefix.tag_ = pair_data.first;
        ip_prefix.length_ = cidr_range.length();
        ip_prefix.ip_ = convertToIpv6(cidr_range.ip()->ipv6()->address());
        ipv6_prefixes.push_back(ip_prefix);
      }
    }
  }
  ipv4_trie_.reset(new LcTrieInternal<Ipv4>(ipv4_prefixes, fill_factor, root_branching_factor));
  ipv6_trie_.reset(new LcTrieInternal<Ipv6>(ipv6_prefixes, fill_factor, root_branching_factor));
}

std::string LcTrie::search(const std::string& ip_address) const {
  Address::InstanceConstSharedPtr address_ptr = Utility::parseInternetAddress(ip_address);
  if (address_ptr->ip()->version() == Address::IpVersion::v4) {
    Ipv4 ip = ntohl(address_ptr->ip()->ipv4()->address());
    return ipv4_trie_->search(ip);
  } else {
    Ipv6 ip = convertToIpv6(address_ptr->ip()->ipv6()->address());
    return ipv6_trie_->search(ip);
  }
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
