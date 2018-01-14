#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/network/address.h"

#include "common/common/empty_string.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"

#include "absl/numeric/int128.h"
#include "fmt/format.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

/**
 * Level Compressed Trie for tagging IP addresses. IPv4 and IPv6 addresses are supported without the
 * caller requiring to change calling pattern.
 *
 * Refer to LcTrieInternal for implementation and algorithm details.
 */
class LcTrie {
public:
  /**
   * @param fill_factor supplies the fraction of completeness to use when considering
   *        if a subtrie should be compressed.
   * @param root_branching_factor supplies the branching factor at the root. This is
   *        an optimization as suggested in the paper.
   * @param tag_data supplies the ip tag and the prefixes for associated with that tag.
   */
  LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
         double fill_factor = 0.5, uint32_t root_branching_factor = 0);

  /**
   * Return the tag for the IP range that covers the input, ip_address. Both IPv4 and IPv6 addresses
   * are supported.
   * @param  ip_address supplies the IP address.
   * @return tag from the prefix that encompasses the ip_address. An empty string is returned
   *         if no prefix has the ip_address in range or there are no tags for the IP
   *         version of the ip_address.
   */
  std::string search(const Network::Address::InstanceConstSharedPtr ip_address) const;

private:
  /**
   * Extract n bits from input starting at position p.
   * @param p supplies the position.
   * @param n supplies the number of bits to extract.
   * @param input supplies the IP address to extract bits from. The IP address is stored in host
   * byte order.
   * @return extracted bits in the format of IpType.
   */
  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)>
  static IpType extractBits(uint32_t p, uint32_t n, IpType input) {
    // The IP's are stored in host byte order.
    // By shifting the value to the left by p bits(and back), the bits between 0 and p-1 are zero'd
    // out. Then to get the n bits, shift the IP back by the address_size minus the number of
    // desired bits.
    return input << p >> (address_size - n);
  }

  /**
   * Removes n bits from input starting at 0.
   * @param n supplies the number of bits to remove.
   * @param input supplies the IP address to remove bits from. The IP address is stored in host
   * byte order.
   * @return input with 0 through n-1 bits cleared.
   */
  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)>
  static IpType removeBits(uint32_t n, IpType input) {
    // The IP's are stored in host byte order.
    // By shifting the value to the left by n bits and back, the bits between 0 and n-1
    // (inclusively) are zero'd out..
    return input << n >> n;
  }

  // IP addresses are stored in host byte order to simplify
  typedef uint32_t Ipv4;
  typedef absl::uint128 Ipv6;

  static std::string toString(const Ipv4& input) {
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(input);
    address.sin_port = htons(0);
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, str, INET_ADDRSTRLEN);
    return str;
  }

  static std::string toString(const Ipv6& input) {
    sockaddr_in6 address;
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(0);
    std::array<uint8_t,16> ip6 = ipv6ToArray(input);
    for(size_t i = 0 ; i < 16 ; i++) {
      address.sin6_addr.s6_addr[i] = ip6[i];
    }

    char str[INET6_ADDRSTRLEN];
    // FIXME: check the result
    inet_ntop(AF_INET6, &address.sin6_addr, str, INET6_ADDRSTRLEN);

    return str;
  }

  /**
   * Structure to hold a CIDR range and the tag associated with it.
   */
  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)> struct IpPrefix {

    /**
     * @return -1 if the current object is less than other. 0 if they are the same. 1
     * if other is smaller than the current object.
     */
    int compare(const IpPrefix& other) const {
      {
        if (ip_ < other.ip_) {
          return -1;
        } else if (ip_ > other.ip_) {
          return 1;
        } else if (length_ < other.length_) {
          return -1;
        } else if (length_ > other.length_) {
          return 1;
        } else {
          return 0;
        }
      }
    }

    bool operator<(const IpPrefix& other) const { return (this->compare(other) == -1); }

    /**
     * @return true if other is a prefix of this.
     */
    bool isPrefix(const IpPrefix& other) {
      return (length_ == 0 || (length_ <= other.length_ &&
                               extractBits<IpType, address_size>(0, length_, ip_) ==
                                   extractBits<IpType, address_size>(0, length_, other.ip_)));
    }

    std::string asString() { return fmt::format("{}/{}", toString(ip_), length_); }

    // The address represented either in uint32_t or uint128.
    IpType ip_{0};
    // Length of the cidr range.
    int length_;
    // TODO(ccaraman): Support more than one tag per entry.
    // Tag for this entry.
    std::string tag_;
  };

  /**
   * Level Compressed Trie (LC-Trie) implementation for IP(IPv4 or IPv6) prefixes.
   *
   * The following is an implementation of the algorithm described in the paper
   * 'IP-address lookup using LC-tries' by'S. Nilsson' and 'G. Karlsson'.
   *
   * 'https://github.com/beevek/libkrb/blob/master/krb/lc_trie.hpp' and
   * 'http://www.csc.kth.se/~snilsson/software/router/C/' were used as reference during
   * implementation.
   *
   * Note: The trie can only support up 524288(2^19) nodes.
   */
  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)> class LcTrieInternal {
  public:
    /**
     * Construct a LC-Trie for IpType.
     * @param tag_data supplies the tag and the CIDR ranges for that tag.
     * @param fill_factor supplies the fraction of completeness to use when considering
     *        if a subtrie should be compressed.
     * @param root_branching_factor supplies the branching factor at the root. This is
     *        an optimization as suggested in the paper.
     */
    LcTrieInternal(std::vector<IpPrefix<IpType>>& tag_data, double fill_factor = 0.5,
                   uint32_t root_branching_factor = 0);

    /**
     * Search the LC-Trie for a prefix that has the input, ip in range and return the tag for the prefix.
     * @param  ip_address supplies the IP address in host byte order.
     * @return tag from the prefix that encompasses the input. An empty string is returned
     *         if no prefix in the LC Trie exists.
     */
    std::string search(const IpType& ip_address) const;

  private:
    /**
     * Builds the Level Compresesed Trie, by first sorting the base vector, removing duplicated
     * prefixes and invoking buildRecursively() to build the trie.
     */
    void build(std::vector<IpPrefix<IpType>>& tag_data) {
      if (tag_data.empty()) {
        return;
      }

      // LcNode uses the last 20 bits to store either the index into ip_prefixes_ or trie_.
      // In theory, the trie_ should only need twice the amount of entries of CIDR ranges.
      // To prevent index out of bounds issues, only support a maximum of 2^19 CIDR ranges.
      // TODO(ccaraman): Add a test case for this.
      if (tag_data.size() > (1 << 19)) {
        throw EnvoyException(fmt::format(
            "Size of the input vector({0}) for LC-Trie is larger than the max size supported(2^19).",
            tag_data.size()));
      }

      std::sort(tag_data.begin(), tag_data.end());
      ip_prefixes_.push_back(tag_data[0]);

      // Remove duplicate entries and check for nested prefixes.
      for (size_t i = 1; i < tag_data.size(); ++i) {
        // TODO(ccaraman): Add support for nested prefixes.
        if (tag_data[i - 1].isPrefix(tag_data[i])) {
          throw EnvoyException(fmt::format(
              "LcTrie does not support nested prefixes. '{0}' is a nested prefix of '{1}'.",
              tag_data[i].asString(), tag_data[i - 1].asString()));
        }

        if (tag_data[i - 1].compare(tag_data[i]) != 0) {
          ip_prefixes_.push_back(tag_data[i]);
        }
      }

      // In theory, the trie_ vector can have at most twice the number of entries in the ip_prefixes_ vector.
      // However, due to the fill factor a buffer is added to the size of the trie_.
      // The buffer value(2000000) is reused from the reference implementation in
      // http://www.csc.kth.se/~snilsson/software/router/C/trie.c.
      // TODO(ccaraman): Define a better buffer value when resizing the trie_.
      trie_.resize(2 * ip_prefixes_.size() + 2000000);

      // Build the trie_.
      uint32_t next_free_index = 1;
      buildRecursive(0u, 0u, ip_prefixes_.size(), 0u, next_free_index);

      // The value of next_free_index is the final size of the trie_.
      trie_.resize(next_free_index);
    }

    // Thin wrapper around computeBranch output to facilitate code readability.
    struct ComputePair {
      ComputePair(int branch, int prefix) : branch_(branch), prefix_(prefix) {}

      uint32_t branch_;
      uint32_t prefix_;
    };

    /**
     * Compute the branch and skip values for the trie starting at position 'first' through
     * 'first+n' while disregarding the prefix.
     * @param prefix supplies the common prefix in the ip_prefixes_ array.
     * @param first supplies the index where computing the branch should begin with.
     * @param n supplies the number of nodes to use while computing the branch.
     * @return pair of integers for the branching factor and the skip.
     */
    ComputePair computeBranch(uint32_t prefix, uint32_t first, uint32_t n) const {
      ComputePair compute(0, 0);

      // Compute the new prefix for the range between ip_prefixes_[first] and
      // ip_prefixes_[first + n - 1].
      IpType high = removeBits<IpType, address_size>(prefix, ip_prefixes_[first].ip_);
      IpType low = removeBits<IpType, address_size>(prefix, ip_prefixes_[first + n - 1].ip_);
      uint32_t index = prefix;

      // Find the index at which low and high diverge to get the skip.
      while (extractBits<IpType, address_size>(index, 1, low) ==
             extractBits<IpType, address_size>(index, 1, high)) {
        ++index;
      }
      compute.prefix_ = index;

      // For 2 elements, use a branching factor of 2(2^1).
      if (n == 2) {
        compute.branch_ = 1;
        return compute;
      }

      // According to the original LC-Trie paper, a large branching factor(es 16)
      // at the root increases performance.
      if (root_branching_factor_ > 0 && prefix == 0 && first == 0) {
        compute.branch_ = root_branching_factor_;
        return compute;
      }

      // Compute the number of bits required for branching by checking for all
      // patterns (b=2 {00, 01, 10, 11}; b=3 {000,001,010,011,100,101,110,111}, etc)
      // are covered in the set.
      uint32_t branch = 1;
      uint32_t count;
      do {
        ++branch;

        // Check if the current branch factor with the fill factor can contain all of the nodes
        // in the current range or if the current branching factor is larger than the
        // IP address_size.
        if (n < fill_factor_ * (1 << branch) ||
            static_cast<uint32_t>(compute.prefix_ + branch) > address_size) {
          break;
        }

        // Start by checking the bit patterns at ip_prefixes_[first] through
        // ip_prefixes_[first + n-1].
        index = first;
        // Pattern to search for.
        uint32_t pattern = 0;
        // Number of patterns found while looping through the list.
        count = 0;

        // Search for all patterns(values) within 1<<branch.
        while (pattern < static_cast<uint32_t>(1 << branch)) {
          bool pattern_found = false;
          // Keep on looping until either all nodes in the range have been visited or
          // an IP prefix doesn't match the pattern.
          while (index < first + n &&
                 static_cast<uint32_t>(extractBits<IpType, address_size>(
                     compute.prefix_, branch, ip_prefixes_[index].ip_)) == pattern) {
            ++index;
            pattern_found = true;
          }

          if (pattern_found) {
            ++count;
          }
          ++pattern;
        }
        // Stop iterating once the size of the branch (with the fill factor ratio) is able to
        // contain all of the prefixes within the current range of ip_prefixes_[first] to
        // ip_prefixes_[first+n-1].
      } while (count >= fill_factor_ * (1 << branch));

      // Set the branching factor.
      compute.branch_ = branch - 1;
      return compute;
    }

    /**
     * Recursively build a trie for IP prefixes from position 'first' to 'first+n'.
     * @param prefix supplies the prefix to ignore when building the sub-trie.
     * @param first supplies the ip_prefixes_ index into for this sub-trie.
     * @param n suppplies the number of entries for the sub-trie.
     * @param position supplies the root for this sub-trie.
     * @param next_free_index supplies the next available index in the trie_.
     */
    void buildRecursive(uint32_t prefix, uint32_t first, uint32_t n, uint32_t position,
                        uint32_t& next_free_index) {
      // Setting a leaf, the branch and skip are 0.
      if (n == 1) {
        trie_[position].address_ = first;
        return;
      }

      ComputePair output = computeBranch(prefix, first, n);

      uint32_t address = next_free_index;
      trie_[position].branch_ = output.branch_;
      trie_[position].skip_ = output.prefix_ - prefix;
      trie_[position].address_ = address;

      // The next available free index to populate in the trie_ is at next_free_index + 2^(branching
      // factor).
      next_free_index += 1 << output.branch_;

      uint32_t new_position = first;

      // Build the subtrees.
      for (uint32_t bit_pattern = 0; bit_pattern < static_cast<uint32_t>(1 << output.branch_);
           ++bit_pattern) {

        // count is the number of entries in the base vector that have the same bit pattern as the
        // ip_prefixes_[new_position].
        int count = 0;
        while (new_position + count < first + n &&
               static_cast<uint32_t>(extractBits<IpType, address_size>(
                   output.prefix_, output.branch_, ip_prefixes_[new_position + count].ip_)) ==
                   bit_pattern) {
          ++count;
        }

        // FIXME add comments here.
        if (count == 0) {
          // FIXME why is it that we then go to position - 1 when new_position == first + n.
          if (new_position == first + n) {
            buildRecursive(output.prefix_ + output.branch_, new_position - 1, 1,
                           address + bit_pattern, next_free_index);
          } else {
            buildRecursive(output.prefix_ + output.branch_, new_position, 1, address + bit_pattern,
                           next_free_index);
          }
        } else if (count == 1 &&
                   ip_prefixes_[new_position].length_ - output.prefix_ < output.branch_) {
          // FIXME output.branch_(number of bits) + output.prefix_(number of bitstotal skipped) is the
          // FIXME
          uint32_t bits = output.branch_ + output.prefix_ - ip_prefixes_[new_position].length_;
          for (uint32_t i = bit_pattern; i < bit_pattern + (1 << bits); ++i) {
            buildRecursive(output.prefix_ + output.branch_, new_position, 1, address + i,
                           next_free_index);
          }
        } else {
          // Recursively build sub-tries for ip_prefixes_[new_position] to
          // ip_prefixes_[new_position+count].
          buildRecursive(output.prefix_ + output.branch_, new_position, count,
                         address + bit_pattern, next_free_index);
        }
        new_position += count;
      }
    }

    /**
     * LcNode is a uint32_t. A wrapper is provided to simplify getting/setting the branch, the skip and
     * the address values held within the structure.
     *
     * The LcNode has three parts to it
     * - Branch: the first 5 bits represent the branching factor. The branching factor is used to determine
     * the number of descendants for the current node. The number represents a power of 2, so there can be
     * at most 2^31 descendant nodes.
     * - Skip: the next 7 bits represent the number of bits to skip when looking at an IP address.
     * This value can be between 0 and 127, so IPv6 is supported.
     * - Address: the remaining 20 bits represent an index either into the trie_ or the ip_prefixes_.
     * If branch_ != 0, the index is for the trie_. If branch == zero, the index is for the ip_prefixes_.
     *
     * Note: If more than 2^19 CIDR ranges are to be stored in trie_, uint64_t should be used instead.
     */
    struct LcNode {
      uint32_t branch_ : 5;
      uint32_t skip_ : 7;
      uint32_t address_ : 20;
    };

    // Vector that contains the CIDR ranges and tags.
    std::vector<IpPrefix<IpType>> ip_prefixes_;

    // Main trie search structure.
    std::vector<LcNode> trie_;

    const double fill_factor_;
    const uint32_t root_branching_factor_;
  };

  /**
   * Converts Ipv6 address in std::array<uint8_t, 16> to absl::uint128 in host byte order.
   * TODO (ccaraman): Remove this workaround once Ipv6Helper returns absl::uint128t.
   * @param address IPv6 address in std::array<uint8_t,16> format.
   * @return IPv6 address in host byte order.
   */
  static Ipv6 arrayToIpv6(const std::array<uint8_t, 16>& address) {
    Ipv6 ipv6{0}; //The default constructor for absl::uint128 doesn't set it to 0.
    size_t i = 0;
    for(;i < address.size() -1 ; i++){
      ipv6 |= static_cast<absl::uint128>(address[i]);
      ipv6 <<= 8;
    }
    ipv6 |= static_cast<absl::uint128>(address[i]);
    return ipv6;
  }

  /**
   * Converts Ipv6 adddress in abs::uint128 to std::array<uint8_t, 16> in network byte order.
   * @param input
   * @return
   */
  static std::array<uint8_t, 16> ipv6ToArray(const Ipv6& address) {
    Ipv6 copy_address = address;
    std::array<uint8_t, 16> ipv6;
    size_t i = ipv6.size() -1;
    for(;0 < i; i--){
      ipv6[i] = static_cast<uint8_t>(copy_address & 0x000000000000000000000000000000FF);
      copy_address >>= 8;
    }
    ipv6[i] = static_cast<uint8_t>(copy_address & 0x000000000000000000000000000000FF);
    return ipv6;
  }

  std::unique_ptr<LcTrieInternal<Ipv4>> ipv4_trie_;
  std::unique_ptr<LcTrieInternal<Ipv6>> ipv6_trie_;
};

template <class IpType, uint32_t address_size>
LcTrie::LcTrieInternal<IpType, address_size>::LcTrieInternal(
    std::vector<IpPrefix<IpType>>& tag_data, double fill_factor, uint32_t root_branching_factor)
    : fill_factor_(fill_factor), root_branching_factor_(root_branching_factor) {
  build(tag_data);
}

template <class IpType, uint32_t address_size>
std::string LcTrie::LcTrieInternal<IpType, address_size>::search(const IpType& ip_address) const {
  if (trie_.empty()) {
    return EMPTY_STRING;
  }

  LcNode node = trie_[0];
  uint32_t branch = node.branch_;
  uint32_t position = node.skip_;
  uint32_t address = node.address_;

  // branch == 0 is a leaf node.
  while (branch != 0) {
    // branch is at most 2^5-1= 31 bits to extract so we can safely cast the
    // output of extractBits to uint32_t without any data loss.
    node = trie_[address + static_cast<uint32_t>(
                               extractBits<IpType, address_size>(position, branch, ip_address))];
    position += branch + node.skip_;
    branch = node.branch_;
    address = node.address_;
  }

  // /0 will match all IP addresses.
  if (ip_prefixes_[address].length_ == 0) {
    return ip_prefixes_[address].tag_;
  }

  // Check the input ip is contained by node at ip_prefixes_[adr] by XOR'ing the two values and
  // checking that up until the length of the range the value is 0.
  IpType bitmask = ip_prefixes_[address].ip_ ^ ip_address;
  if (extractBits<IpType, address_size>(0, ip_prefixes_[address].length_, bitmask) == 0) {
    return ip_prefixes_[address].tag_;
  }
  return EMPTY_STRING;
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
