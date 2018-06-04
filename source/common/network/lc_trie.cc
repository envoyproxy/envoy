#include "common/network/lc_trie.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

LcTrie::LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
               double fill_factor, uint32_t root_branching_factor) {

  // The LcTrie implementation uses 20-bit "pointers" in its compact internal representation,
  // so it cannot hold more than 2^20 nodes. But the number of nodes can be greater than the
  // number of supported prefixes. Given N prefixes in the tag_data input list, step 2 below
  // can produce a new list of up to 2*N prefixes to insert in the LC trie. And the LC trie
  // can use up to 2*N/fill_factor nodes.
  size_t num_prefixes = 0;
  for (const auto& tag : tag_data) {
    num_prefixes += tag.second.size();
  }
  const size_t max_prefixes = MaxLcTrieNodes * fill_factor / 2;
  if (num_prefixes > max_prefixes) {
    throw EnvoyException(fmt::format("The input vector has '{0}' CIDR range entries. LC-Trie "
                                     "can only support '{1}' CIDR ranges with the specified "
                                     "fill factor.",
                                     num_prefixes, max_prefixes));
  }

  // Step 1: separate the provided prefixes by protocol (IPv4 vs IPv6),
  // and build a Binary Trie per protocol.
  //
  // For example, if the input prefixes are
  //   A: 0.0.0.0/0
  //   B: 128.0.0.0/2  (10000000.0.0.0/2 in binary)
  //   C: 192.0.0.0/2  (11000000.0.0.0/2)
  // the Binary Trie for IPv4 will look like this at the end of step 1:
  //          +---+
  //          | A |
  //          +---+
  //               \ 1
  //              +---+
  //              |   |
  //              +---+
  //            0/     \1
  //          +---+   +---+
  //          | B |   | C |
  //          +---+   +---+
  //
  // Note that the prefixes in this example are nested: any IPv4 address
  // that matches B or C will also match A. Unfortunately, the classic LC Trie
  // algorithm does not support nested prefixes. The next step will solve that
  // problem.

  BinaryTrie<Ipv4> ipv4_temp;
  BinaryTrie<Ipv6> ipv6_temp;
  for (const auto& pair_data : tag_data) {
    for (const auto& cidr_range : pair_data.second) {
      if (cidr_range.ip()->version() == Address::IpVersion::v4) {
        IpPrefix<Ipv4> ip_prefix(ntohl(cidr_range.ip()->ipv4()->address()), cidr_range.length(),
                                 pair_data.first);
        ipv4_temp.insert(ip_prefix);
      } else {
        IpPrefix<Ipv6> ip_prefix(Utility::Ip6ntohl(cidr_range.ip()->ipv6()->address()),
                                 cidr_range.length(), pair_data.first);
        ipv6_temp.insert(ip_prefix);
      }
    }
  }

  // Step 2: push each Binary Trie's prefixes to its leaves.
  //
  // Continuing the previous example, the Binary Trie will look like this
  // at the end of step 2:
  //          +---+
  //          |   |
  //          +---+
  //        0/     \ 1
  //      +---+   +---+
  //      | A |   |   |
  //      +---+   +---+
  //            0/     \1
  //          +---+   +---+
  //          |A,B|   |A,C|
  //          +---+   +---+
  //
  // This trie yields the same match results as the original trie from
  // step 1. But it has a useful new property: now that all the prefixes
  // are at the leaves, they are disjoint: no prefix is nested under another.

  std::vector<IpPrefix<Ipv4>> ipv4_prefixes = ipv4_temp.push_leaves();
  std::vector<IpPrefix<Ipv6>> ipv6_prefixes = ipv6_temp.push_leaves();

  // Step 3: take the disjoint prefixes from the leaves of each Binary Trie
  // and use them to construct an LC Trie.
  //
  // Example inputs (from the leaves of the Binary Trie at the end of step 2)
  //   A:   0.0.0.0/1
  //   A,B: 128.0.0.0/2
  //   A,C: 192.0.0.0/2
  //
  // The LC Trie generated from these inputs with fill_factor=0.5 and root_branching_factor=0
  // will be:
  //
  //       +---------------------------+
  //       | branch_factor=2, skip = 0 |
  //       +---------------------------+
  //    00/       01|         |10       \11
  //   +---+      +---+     +---+      +---+
  //   | A |      | A |     |A,B|      |A,C|
  //   +---+      +---+     +---+      +---+
  //
  // Or, in the internal vector form that the LcTrie class uses for memory-efficiency,
  //    # | branch | skip | first_child | tags | note
  //   ---+--------+------+-------------+------+--------------------------------------------------
  //    0 |      2 |    0 |           1 |  -   | (1 << branch) == 4 children, starting at offset 1
  //    1 |      - |    0 |           - |  A   | 1st child of node 0, reached if next bits are 00
  //    2 |      - |    0 |           - |  A   |   .
  //    3 |      - |    0 |           - |  A,B |   .
  //    4 |      - |    0 |           - |  A,C | 4th child of node 0, reached if next bits are 11
  //
  // The Nilsson and Karlsson paper linked in lc_trie.h has a more thorough example.

  ipv4_trie_.reset(new LcTrieInternal<Ipv4>(ipv4_prefixes, fill_factor, root_branching_factor));
  ipv6_trie_.reset(new LcTrieInternal<Ipv6>(ipv6_prefixes, fill_factor, root_branching_factor));
}

std::vector<std::string>
LcTrie::getTags(const Network::Address::InstanceConstSharedPtr& ip_address) const {
  if (ip_address->ip()->version() == Address::IpVersion::v4) {
    Ipv4 ip = ntohl(ip_address->ip()->ipv4()->address());
    return ipv4_trie_->getTags(ip);
  } else {
    Ipv6 ip = Utility::Ip6ntohl(ip_address->ip()->ipv6()->address());
    return ipv6_trie_->getTags(ip);
  }
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
