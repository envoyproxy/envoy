#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/network/address.h"

#include "common/network/cidr_range.h"
#include "common/network/utility.h"

#include "absl/numeric/int128.h"
#include "fmt/format.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

/**
 * Level Compressed Trie implemenation for IP(Ipv4 or IPv6) Prefixes.
 *
 * Note: The C++ implementation of the LcTrie found at
 * https://github.com/beevek/libkrb/blob/master/krb/lc_trie.hpp was used as a reference for this
 * file. The C++ implementation was based off of S. Nilson's code at
 * http://www.csc.kth.se/~snilsson/software/router/C/
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
   * Search the LC Trie for a prefix that has the input, ip, in range.
   * @param  ip supplies the IP address searching for a prefix in string format.
   * @return tag from the prefix that encompasses the input. An empty string is returned
   *         if no prefix in the LC Trie exists.
   */
  std::string search(const std::string& ip_address) const;

private:
  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)> struct IpPrefixes {
    /**
     * @return -1 if the current object is less than b. 0 if they are the same. 1
     * if b is smaller than the current object.
     */
    int compare(const IpPrefixes& b) const {
      {
        if (ip_ < b.ip_)
          return -1;
        else if (ip_ > b.ip_)
          return 1;
        else if (length_ < b.length_)
          return -1;
        else if (length_ > b.length_)
          return 1;
        else
          return 0;
      }
    }

    bool operator<(const IpPrefixes& b) const { return (this->compare(b) == -1); }

    // TODO(ccaraman) remove duplicate
    static IpType extractBits(int p, int n, IpType input) {
      return input << p >> (address_size - n);
    }

    bool isPrefix(const IpPrefixes& b) {
      return (length_ == 0 || (length_ <= b.length_ &&
                               extractBits(0, length_, ip_) == extractBits(0, length_, b.ip_)));
    }

    // The address represented either in uint32_t or uint128.
    IpType ip_;
    // Length of the cidr range.
    int length_;
    // TODO(ccaraman): Support more than one tag per entry.
    // Tag for this entry.
    std::string tag_;
  };

  template <class IpType, uint32_t address_size = 8 * sizeof(IpType)> class LcTrieInternal {
  public:
    /**
     * Level Compress Trie (LC-Trie) implementation for IP(IPv4 or IPv6) prefixes. Note: this trie
     * can handle Ipv4/Ipv6 separately.
     *
     * Note: The trie can only support up 524288(2^19) nodes. Please see LcNode for further
     * explanation.
     *
     * @param fill_factor supplies the fraction of completeness to use when considering
     *        if a subtrie should be compressed.
     * @param root_branching_factor supplies the branching factor at the root. This is
     *        an optimization as suggested in the paper.
     * @param tag_data supplies the ip tag and the prefixes for associated with that tag.
     */
    LcTrieInternal(std::vector<IpPrefixes<IpType>>& tag_data, double fill_factor = 0.5,
                   uint32_t root_branching_factor = 0);

    /**
     * Search the LC Trie for a prefix that has the input, ip, in range.
     * @param  ip supplies the IP address searching for a prefix in string format.
     * @return tag from the prefix that encompasses the input. An empty string is returned
     *         if no prefix in the LC Trie exists.
     */
    std::string search(const IpType& ip_address) const;

  private:
    /**
     * // fIXME Duplicate
     * Extract n bits from input starting at position p.
     * @param position supplies the position.
     * @param n supplies the number of bits to extract.
     * @param input supplies the
     * @return
     */
    static IpType extractBits(int p, int n, IpType input) {
      return input << p >> (address_size - n);
    }

    /**
     * Removes n bits from input starting at 0.
     * @param n supplies the number of bits to remove.
     * @param input supplies the
     * @return
     */
    static IpType removeBits(int n, IpType input) { return input << n >> n; }

    /**
     * Builds the Level Compresesed Trie, by first sorting the base vector, removing nested prefixes
     * and duplicated and then recursively builds the trie by invoking buildRecursively().
     */
    // this will take the ip_prefixes.
    void buildLcTrie(std::vector<IpPrefixes<IpType>>& tag_data) {
      if (tag_data.empty()) {
        return;
      }

      // The node used for the trie uses the last 20 bits of uint32_t. The trie cannot support
      // an input greater than 524288(2^19) nodes.
      // TODO(ccaraman) Add a test case for this.
      if (tag_data.size() > (1 << 19)) {
        throw new EnvoyException(fmt::format(
            "Size of the input vector({0}) for LC-Trie is larger than the max size supported(2^19)",
            tag_data.size()));
      }

      std::sort(tag_data.begin(), tag_data.end());
      ip_prefixes_.push_back(tag_data[0]);

      // Remove duplicate entries and nested prefixes.
      for (size_t i = 1; i < tag_data.size(); ++i) {
        // TODO(ccaraman) Add support for nested prefixes.
        if (tag_data[i - 1].compare(tag_data[i]) != 0 && !tag_data[i - 1].isPrefix(tag_data[i])) {
          ip_prefixes_.push_back(tag_data[i]);
        }
      }

      // The trie_ vector can have at most twice the number of nodes in the ip_prefixes vector.
      // However, due to the fill factor a buffer is added. Preallocate the size of the vector to
      // avoid resizing while building the trie.
      // TODO(ccaraman) Define a better buffer value for the trie_ size.
      trie_.resize(2 * ip_prefixes_.size() + 2000000);

      uint32_t next_free_index = 1;

      // Build the trie
      buildRecursive(0u, 0u, ip_prefixes_.size(), 0u, &next_free_index);

      // next_free_index value is the next index free in the trie_ vector.
      // Since the trie_ is populated, resize the vector to remove unused memory.
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
     * @param prefix supplies the prefix the common prefix in the base array.
     * @param first supplies the index where computing the branch should begin with.
     * @param n supplies the number of nodes to use while computing the branch.
     * @return pair of integers that represent the branching factor and the new prefix.
     */
    ComputePair computeBranch(uint32_t prefix, uint32_t first, uint32_t n) const {
      IpType low, high;
      uint32_t i, branch, count;
      uint32_t pattern;
      bool pattern_found;

      ComputePair compute(0, 0);

      // compute the new prefix
      high = removeBits(prefix, ip_prefixes_[first].ip_);
      low = removeBits(prefix, ip_prefixes_[first + n - 1].ip_);
      i = prefix;

      // Find the index at which low and high diverge to get the skip.
      while (extractBits(i, 1, low) == extractBits(i, 1, high)) {
        ++i;
      }
      // FIXME explain how this is the new prefix.
      compute.prefix_ = i;

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
      branch = 1;
      // FIXME bad while loop
      do {
        ++branch;

        // Check if the current branch factor with the fill factor can contain all of the nodes
        // in the current range or if the current branching factor will get outside of the address
        // size.
        if (n < fill_factor_ * (1 << branch) ||
            static_cast<uint32_t>(compute.prefix_ + branch) > address_size) {
          break;
        }

        // Start by checking the bit patterns at ip_prefixes_[first] through ip_prefixes_[first +
        // n-1].
        i = first;
        // Pattern to search for.
        pattern = 0;
        // Number of patterns found while looping through the list.
        count = 0;

        // Search for all patterns(values) within 1<<branch.
        while (pattern < static_cast<uint32_t>(1 << branch)) {
          pattern_found = false;
          // Keep on looping until either all elements have been looked at
          // stop looping when the pattern isn't matched anymore or we get the index out of bounds
          while (i < first + n && static_cast<uint32_t>(extractBits(
                                      compute.prefix_, branch, ip_prefixes_[i].ip_)) == pattern) {
            ++i;
            pattern_found = true;
          }
          if (pattern_found) {
            ++count;
          }
          ++pattern;
        }
        // Once the size of the branch(with the fill factor ratio) is able to contain all of the
        // within the current range.
      } while (count >= fill_factor_ * (1 << branch));

      // Set the branching factor.
      compute.branch_ = branch - 1;
      return compute;
    }

    /**
     * Recursively builds a tree that covers the base array from position 'first' to 'first+n'.
     * @param prefix supplies the prefix to ignore when building the tree.
     * @param first supplies the index into the ip_prefixes_ vector where to start building the
     * trie_ from.
     * @param n suppplies the number of entries.
     * @param pos supplies the root for this trie..
     * @param nextfree supplies the next available index in the trie_.
     */
    // FIXME make this return a value instead of c style.
    void buildRecursive(uint32_t prefix, uint32_t first, uint32_t n, uint32_t pos,
                        uint32_t* nextfree) {
      uint32_t p, address, bits;
      uint32_t bit_pattern;
      int k;

      // Setting a leaf, the branch and skip are 0.
      if (n == 1) {
        trie_[pos] = first;
        return;
      }

      ComputePair output = computeBranch(prefix, first, n);

      address = *nextfree;
      trie_[pos] =
          setBranch(output.branch_) | setSkip(output.prefix_ - prefix) | setAddress(address);

      // For the next part of the trie, the next available address is at address + 2^branching
      // factor.
      *nextfree += 1 << output.branch_;
      p = first;

      // build the subtrees
      for (bit_pattern = 0; bit_pattern < static_cast<uint32_t>(1 << output.branch_);
           ++bit_pattern) {

        k = 0;
        // k is finding the number of entries in the base vector that have the same bit pattern.
        while (p + k < first + n &&
               static_cast<uint32_t>(extractBits(output.prefix_, output.branch_,
                                                 ip_prefixes_[p + k].ip_)) == bit_pattern) {
          ++k;
        }

        if (k == 0) {
          if (p == first + n) {
            buildRecursive(output.prefix_ + output.branch_, p - 1, 1, address + bit_pattern,
                           nextfree);
          } else {
            buildRecursive(output.prefix_ + output.branch_, p, 1, address + bit_pattern, nextfree);
          }
        } else if (k == 1 && ip_prefixes_[p].length_ - output.prefix_ < output.branch_) {
          uint32_t i;
          bits = output.branch_ + output.prefix_ - ip_prefixes_[p].length_;
          for (i = bit_pattern; i < bit_pattern + (1 << bits); ++i) {
            buildRecursive(output.prefix_ + output.branch_, p, 1, address + i, nextfree);
          }
        } else {
          buildRecursive(output.prefix_ + output.branch_, p, k, address + bit_pattern, nextfree);
        }
        p += k;
      }
    }

    /**
     * LcNode is a uint32_t. A wrapper is provided to simplify getting the branch, skip and
     * address values held within the data structure.
     *
     * The LcNode has three parts to it
     * - Branch: the first 5 bits represent the branching factor. This supports a branching factor
     * of 2^31.
     * - Skip: the next 7 bits represent the number of bits to skip when looking at an IP address.
     * This value can be between 0 and 127, so IPv6 is supported as well.
     * - Address: the remaining 30 bits represent the pointer. If branch value is not zero, this
     * address points to another entry in the trie. If branch is zero, the address points to an
     * entry in the base vector.
     */
    typedef uint32_t LcNode;
    /**
     * Takes a branch value and shifts it into the first 5 bits of uint32_t.
     */
    static LcNode setBranch(LcNode node) { return node << 27; }

    /**
     * @return Returns the first 5 bits of LcNode.
     */
    static LcNode getBranch(LcNode node) { return node >> 27; }

    /**
     * Takes a skip value and shifts it itno the 6-12(inclusively) bits of uint32_t.
     * @param node
     * @return
     */
    static LcNode setSkip(LcNode node) { return node << 20; }

    /**
     * @return Returns the value at bits 6-12.
     */
    static LcNode getSkip(LcNode node) { return node >> 20 & 127; }

    static LcNode setAddress(LcNode node) { return node; }

    /**
     * @return Returns the last 20 bits of LcNode.
     * Note: 1048575 is the bitmask for 2^19-1.
     */
    static LcNode getAddress(LcNode node) { return node & 1048575; }

    // Vector that contains the IP Prefixes and tags.
    std::vector<IpPrefixes<IpType>> ip_prefixes_;

    // Main trie search structure.
    std::vector<LcNode> trie_;

    const double fill_factor_;
    const uint32_t root_branching_factor_;
  };

  typedef uint32_t Ipv4;
  typedef absl::uint128 Ipv6;

  /**
   * Converts Ipv6 address in std::array<uint8_t, 16> to absl::uint128 in ntohl order.
   * TODO (ccaraman) Remove this workaround once Ipv6Helper returns absl::uint128t.
   */
  static Ipv6 convertToIpv6(const std::array<uint8_t, 16>& address) {
    Ipv6 ipv6;
    ipv6 = static_cast<absl::uint128>(address[7]) << 64 |
           (static_cast<absl::uint128>(address[6]) << 72) |
           (static_cast<absl::uint128>(address[5]) << 80) |
           (static_cast<absl::uint128>(address[4]) << 88) |
           (static_cast<absl::uint128>(address[3]) << 96) |
           (static_cast<absl::uint128>(address[2]) << 104) |
           (static_cast<absl::uint128>(address[1]) << 112) |
           (static_cast<absl::uint128>(address[0]) << 120);
    ipv6 |= static_cast<absl::uint128>(address[15]) |
            (static_cast<absl::uint128>(address[14]) << 8) |
            (static_cast<absl::uint128>(address[13]) << 16) |
            (static_cast<absl::uint128>(address[12]) << 24) |
            (static_cast<absl::uint128>(address[11]) << 32) |
            (static_cast<absl::uint128>(address[10]) << 40) |
            (static_cast<absl::uint128>(address[9]) << 48) |
            (static_cast<absl::uint128>(address[8]) << 56);

    return ipv6;
  }

  std::unique_ptr<LcTrieInternal<Ipv4>> ipv4_trie_;
  std::unique_ptr<LcTrieInternal<Ipv6>> ipv6_trie_;
};

template <class IpType, uint32_t address_size>
LcTrie::LcTrieInternal<IpType, address_size>::LcTrieInternal(
    std::vector<IpPrefixes<IpType>>& tag_data, double fill_factor, uint32_t root_branching_factor)
    : fill_factor_(fill_factor), root_branching_factor_(root_branching_factor) {
  buildLcTrie(tag_data);
}

template <class IpType, uint32_t address_size>
std::string LcTrie::LcTrieInternal<IpType, address_size>::search(const IpType& ip_address) const {
  if (trie_.empty()) {
    return "";
  }

  LcNode node;
  uint32_t position, branch, address;
  IpType bitmask;

  // Starting from the root, get the branching factor, skip value and the next address.
  node = trie_[0];
  branch = getBranch(node);
  position = getSkip(node);
  address = getAddress(node);
  while (branch != 0) {
    // branch value is at most 2^5-1= 31 bits to extract so we can safely cast the
    // output of EXTRACT to uint32_t without any data loss.
    node = trie_[address + static_cast<uint32_t>(extractBits(position, branch, ip_address))];
    position += branch + getSkip(node);
    branch = getBranch(node);
    address = getAddress(node);
  }

  // Check the input ip is contained by node at ip_prefixes_[adr] by XOR'ing the two values and
  // checking that up until the length of the range the value is 0.
  bitmask = ip_prefixes_[address].ip_ ^ ip_address;
  if (extractBits(0, ip_prefixes_[address].length_, bitmask) == 0) {
    return ip_prefixes_[address].tag_;
  }
  return "";
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
