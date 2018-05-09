#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <climits>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/network/address.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/utility.h"

#include "absl/numeric/int128.h"
#include "fmt/format.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

/**
 * Level Compressed Trie for tagging IP addresses. Both IPv4 and IPv6 addresses are supported
 * within this class with no calling pattern changes.
 *
 * The algorithm to build the LC-Trie is desribed in the paper 'IP-address lookup using LC-tries'
 * by 'S. Nilsson' and 'G. Karlsson'. The paper and reference C implementation can be found here:
 * https://www.nada.kth.se/~snilsson/publications/IP-address-lookup-using-LC-tries/
 *
 * Refer to LcTrieInternal for implementation and algorithm details.
 */
class LcTrie {
public:
  /**
   * @param tag_data supplies a vector of tag and CIDR ranges.
   * @param fill_factor supplies the fraction of completeness to use when calculating the branch
   *                    value for a sub-trie.
   * @param root_branching_factor supplies the branching factor at the root.
   *
   * TODO(ccaraman): Investigate if a non-zero root branching factor should be the default. The
   * paper suggests for large LC-Tries to use the value '16'. It reduces the depth of the trie.
   * However, there is no suggested values for smaller LC-Tries. With perf tests, it is possible to
   * get this data for smaller LC-Tries. Another option is to expose this in the configuration and
   * let consumers decide.
   */
  LcTrie(const std::vector<std::pair<std::string, std::vector<Address::CidrRange>>>& tag_data,
         double fill_factor = 0.5, uint32_t root_branching_factor = 0);

  /**
   * Retrieve the tag associated with the CIDR range that contains `ip_address`. Both IPv4 and IPv6
   * addresses are supported.
   * @param  ip_address supplies the IP address.
   * @return a vector of tags from the CIDR ranges and IP addresses that contains 'ip_address'. An
   * empty vector is returned if no prefix contains 'ip_address' or there is no data for the IP
   * version of the ip_address.
   */
  std::vector<std::string>
  getTags(const Network::Address::InstanceConstSharedPtr& ip_address) const;

private:
  /**
   * Extract n bits from input starting at position p.
   * @param p supplies the position.
   * @param n supplies the number of bits to extract.
   * @param input supplies the IP address to extract bits from. The IP address is stored in host
   *              byte order.
   * @return extracted bits in the format of IpType.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)>
  static IpType extractBits(uint32_t p, uint32_t n, IpType input) {
    // The IP's are stored in host byte order.
    // By shifting the value to the left by p bits(and back), the bits between 0 and p-1 are zero'd
    // out. Then to get the n bits, shift the IP back by the address_size minus the number of
    // desired bits.
    if (n == 0) {
      return IpType(0);
    }
    return input << p >> (address_size - n);
  }

  /**
   * Removes n bits from input starting at 0.
   * @param n supplies the number of bits to remove.
   * @param input supplies the IP address to remove bits from. The IP address is stored in host
   *              byte order.
   * @return input with 0 through n-1 bits cleared.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)>
  static IpType removeBits(uint32_t n, IpType input) {
    // The IP's are stored in host byte order.
    // By shifting the value to the left by n bits and back, the bits between 0 and n-1
    // (inclusively) are zero'd out.
    return input << n >> n;
  }

  // IP addresses are stored in host byte order to simplify
  typedef uint32_t Ipv4;
  typedef absl::uint128 Ipv6;

  typedef std::unordered_set<std::string> TagSet;
  typedef std::shared_ptr<std::unordered_set<std::string>> TagSetSharedPtr;

  /**
   * Structure to hold a CIDR range and the tag associated with it.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)> struct IpPrefix {

    IpPrefix() {}

    IpPrefix(const IpType& ip, uint32_t length, const std::string& tag) : ip_(ip), length_(length) {
      tags_.insert(tag);
    }

    IpPrefix(const IpType& ip, int length, const TagSet& tags)
        : ip_(ip), length_(length), tags_(tags) {}

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

    bool operator!=(const IpPrefix& other) const { return (this->compare(other) != 0); }

    /**
     * @return true if other is a prefix of this.
     */
    bool isPrefix(const IpPrefix& other) {
      return (length_ == 0 || (length_ <= other.length_ && contains(other.ip_)));
    }

    /**
     * @param address supplies an IP address to check against this prefix.
     * @return bool true if this prefix contains the address.
     */
    bool contains(const IpType& address) const {
      return (extractBits<IpType, address_size>(0, length_, ip_) ==
              extractBits<IpType, address_size>(0, length_, address));
    }

    std::string asString() { return fmt::format("{}/{}", toString(ip_), length_); }

    // The address represented either in Ipv4(uint32_t) or Ipv6(asbl::uint128).
    IpType ip_{0};
    // Length of the cidr range.
    uint32_t length_{0};
    // Tag(s) for this entry.
    TagSet tags_;
  };

  /**
   * Binary trie used to simplify the construction of Level Compressed Tries.
   * This data type supports two operations:
   *   1. Add a prefix to the trie.
   *   2. Push the prefixes to the leaves of the trie.
   * That second operation produces a new set of prefixes that yield the same
   * match results as the original set of prefixes from which the BinaryTrie
   * was constructed, but with an important difference: the new prefixes are
   * guaranteed not to be nested within each other. That allows the use of the
   * classic LC Trie construction algorithm, which is fast and (relatively)
   * simple but does not work properly with nested prefixes.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)> class BinaryTrie {
  public:
    BinaryTrie() : root_(std::make_unique<Node>()) {}

    /**
     * Add a CIDR prefix and associated tag to the binary trie. If an entry already
     * exists for the prefix, merge the tag into the existing entry.
     */
    void insert(const IpPrefix<IpType>& prefix) {
      Node* node = root_.get();
      for (uint32_t i = 0; i < prefix.length_; i++) {
        auto bit = static_cast<uint32_t>(extractBits(i, 1, prefix.ip_));
        NodePtr& next_node = node->children[bit];
        if (next_node == nullptr) {
          next_node = std::make_unique<Node>();
        }
        node = next_node.get();
      }
      if (node->tags == nullptr) {
        node->tags = std::make_shared<TagSet>();
      }
      node->tags->insert(prefix.tags_.begin(), prefix.tags_.end());
    }

    /**
     * Update each node in the trie to inherit/override its ancestors' tags,
     * and then push the prefixes in the binary trie to the leaves so that:
     *  1) each leaf contains a prefix, and
     *  2) given the set of prefixes now located at the leaves, a useful
     *     new property applies: no prefix in that set is nested under any
     *     other prefix in the set (since, by definition, no leaf of the
     *     trie can be nested under another leaf)
     * @return the prefixes associated with the leaf nodes.
     */
    std::vector<IpPrefix<IpType>> push_leaves() {
      std::vector<IpPrefix<IpType>> prefixes;
      std::function<void(Node*, TagSetSharedPtr, unsigned, IpType)> visit =
          [&](Node* node, TagSetSharedPtr tags, unsigned depth, IpType prefix) {
            // Inherit any tags set by ancestor nodes.
            if (tags != nullptr) {
              if (node->tags == nullptr) {
                node->tags = tags;
              } else {
                node->tags->insert(tags->begin(), tags->end());
              }
            }
            // If a node has exactly one child, create a second child node
            // that inherits the union of all tags set by any ancestor nodes.
            // This gives the trie an important new property: all the configured
            // prefixes end up at the leaves of the trie. As no leaf is nested
            // under another leaf (or one of them would not be a leaf!), the
            // leaves of the trie upon completion of this leaf-push operation
            // will form a set of disjoint prefixes (no nesting) that can be
            // used to build an LC trie.
            if (node->children[0] != nullptr && node->children[1] == nullptr) {
              node->children[1] = std::make_unique<Node>();
            } else if (node->children[0] == nullptr && node->children[1] != nullptr) {
              node->children[0] = std::make_unique<Node>();
            }
            if (node->children[0] != nullptr) {
              visit(node->children[0].get(), node->tags, depth + 1, (prefix << 1) + IpType(0));
              visit(node->children[1].get(), node->tags, depth + 1, (prefix << 1) + IpType(1));
            } else {
              if (node->tags != nullptr) {
                // Compute the CIDR prefix from the path we've taken to get to this point in the
                // tree.
                IpType ip = prefix;
                if (depth != 0) {
                  ip <<= (address_size - depth);
                }
                prefixes.emplace_back(IpPrefix<IpType>(ip, depth, *node->tags));
              }
            }
          };
      visit(root_.get(), nullptr, 0, IpType(0));
      return prefixes;
    }

  private:
    struct Node {
      std::unique_ptr<Node> children[2];
      TagSetSharedPtr tags;
    };
    typedef std::unique_ptr<Node> NodePtr;
    NodePtr root_;
  };

  /**
   * Level Compressed Trie (LC-Trie) that contains CIDR ranges and its corresponding tags.
   *
   * The following is an implementation of the algorithm described in the paper
   * 'IP-address lookup using LC-tries' by'S. Nilsson' and 'G. Karlsson'.
   *
   * 'https://github.com/beevek/libkrb/blob/master/krb/lc_trie.hpp' and
   * 'http://www.csc.kth.se/~snilsson/software/router/C/' were used as reference during
   * implementation.
   *
   * Note: The trie can only support up 524288(2^19) prefixes with a fill_factor of 1 and
   * root_branching_factor not set. Refer to LcTrieInternal::build() method for more details.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)> class LcTrieInternal {
  public:
    /**
     * Construct a LC-Trie for IpType.
     * @param tag_data supplies a vector of tag and CIDR ranges (in IpPrefix format).
     * @param fill_factor supplies the fraction of completeness to use when calculating the branch
     *                    value for a sub-trie.
     * @param root_branching_factor supplies the branching factor at the root. The paper suggests
     *                              for large LC-Tries to use the value '16' for the root branching
     *                              factor. It reduces the depth of the trie.
     */
    LcTrieInternal(std::vector<IpPrefix<IpType>>& tag_data, double fill_factor,
                   uint32_t root_branching_factor);

    /**
     * Retrieve the tag associated with the CIDR range that contains `ip_address`.
     * @param  ip_address supplies the IP address in host byte order.
     * @return a vector of tags from the CIDR ranges and IP addresses that encompasses the input. An
     * empty vector is returned if the LC Trie is empty.
     */
    std::vector<std::string> getTags(const IpType& ip_address) const;

  private:
    /**
     * Builds the Level Compresesed Trie, by first sorting the tag data, removing duplicated
     * prefixes and invoking buildRecursive() to build the trie.
     */
    void build(std::vector<IpPrefix<IpType>>& tag_data) {
      if (tag_data.empty()) {
        return;
      }

      // LcNode uses the last 20 bits to store either the index into ip_prefixes_ or trie_.
      // In theory, the trie_ should only need twice the amount of entries of CIDR ranges.
      // To prevent index out of bounds issues, only support a maximum of (2^19) CIDR ranges.
      if (tag_data.size() > MAXIMUM_CIDR_RANGE_ENTRIES) {
        throw EnvoyException(fmt::format("The input vector has '{0}' CIDR ranges entires. LC-Trie "
                                         "can only support '{1}' CIDR ranges.",
                                         tag_data.size(), MAXIMUM_CIDR_RANGE_ENTRIES));
      }

      ip_prefixes_ = tag_data;
      std::sort(ip_prefixes_.begin(), ip_prefixes_.end());

      // In theory, the trie_ vector can have at most twice the number of ip_prefixes entries - 1.
      // However, due to the fill factor a buffer is added to the size of the
      // trie_. The buffer value(2000000) is reused from the reference implementation in
      // http://www.csc.kth.se/~snilsson/software/router/C/trie.c.
      // TODO(ccaraman): Define a better buffer value when resizing the trie_.
      maximum_trie_node_size = 2 * ip_prefixes_.size() + 2000000;
      trie_.resize(maximum_trie_node_size);

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
      // The total number of bits that have the same prefix for subset of ip_prefixes_.
      uint32_t prefix_;
    };

    /**
     * Compute the branch and skip values for the trie starting at position 'first' through
     * 'first+n-1' while disregarding the prefix.
     * @param prefix supplies the common prefix in the ip_prefixes_ array.
     * @param first supplies the index where computing the branch should begin with.
     * @param n supplies the number of nodes to use while computing the branch.
     * @return pair of integers for the branching factor and the skip.
     */
    ComputePair computeBranchAndSkip(uint32_t prefix, uint32_t first, uint32_t n) const {
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

      // According to the original LC-Trie paper, a large branching factor(suggested value: 16)
      // at the root increases performance.
      if (root_branching_factor_ > 0 && prefix == 0 && first == 0) {
        compute.branch_ = root_branching_factor_;
        return compute;
      }

      // Compute the number of bits required for branching by checking all patterns in the set are
      // covered. Ex (b=2 {00, 01, 10, 11}; b=3 {000,001,010,011,100,101,110,111}, etc)
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
        // Stop iterating once the size of the branch (with the fill factor ratio)
        // can no longer contain all of the prefixes within the current range of
        // ip_prefixes_[first] to ip_prefixes_[first+n-1].
      } while (count >= fill_factor_ * (1 << branch));

      // The branching factor is decremented because the algorithm requires the largest branching
      // factor that covers all(most when a fill factor is specified) of the CIDR ranges in the
      // current sub-trie. When the loops above exits, the branch factor value is
      // 1. greater than the address size with the prefix.
      // 2. greater than the number of entries.
      // 3. less than the total number of patterns seen in the range.
      // In all of the cases above, branch - 1 is guaranteed to cover all of CIDR
      // ranges in the sub-trie.
      compute.branch_ = branch - 1;
      return compute;
    }

    /**
     * Recursively build a trie for IP prefixes from position 'first' to 'first+n-1'.
     * @param prefix supplies the prefix to ignore when building the sub-trie.
     * @param first supplies the index into ip_prefixes_ for this sub-trie.
     * @param n suppplies the number of entries for the sub-trie.
     * @param position supplies the root for this sub-trie.
     * @param next_free_index supplies the next available index in the trie_.
     */
    void buildRecursive(uint32_t prefix, uint32_t first, uint32_t n, uint32_t position,
                        uint32_t& next_free_index) {
      // Setting a leaf, the branch and skip are 0.
      if (n == 1) {
        // There is no way to predictably determine the number of trie nodes required to build a
        // LC-Trie. If while building the trie the position that is being set exceeds the maximum
        // number of supported trie_ entries, throw an Envoy Exception.
        if (position >= maximum_trie_node_size) {
          // Adding 1 to the position to count how many nodes are trying to be set.
          throw EnvoyException(
              fmt::format("The number of internal nodes required for the LC-Trie "
                          "exceeded the maximum number of "
                          "supported nodes. Minimum number of internal nodes required: "
                          "'{0}'. Maximum number of supported nodes: '{1}'.",
                          (position + 1), maximum_trie_node_size));
        }

        trie_[position].address_ = first;
        return;
      }

      ComputePair output = computeBranchAndSkip(prefix, first, n);

      uint32_t address = next_free_index;
      trie_[position].branch_ = output.branch_;
      // The skip value is the number of bits between the newly calculated prefix(output.prefix_)
      // and the previous prefix(prefix).
      trie_[position].skip_ = output.prefix_ - prefix;
      trie_[position].address_ = address;

      // The next available free index to populate in the trie_ is at next_free_index + 2^(branching
      // factor).
      next_free_index += 1 << output.branch_;

      uint32_t new_position = first;

      // Build the subtrees.
      for (uint32_t bit_pattern = 0; bit_pattern < static_cast<uint32_t>(1 << output.branch_);
           ++bit_pattern) {

        // count is the number of entries in the ip_prefixes_ vector that have the same bit pattern
        // as the ip_prefixes_[new_position].
        int count = 0;
        while (new_position + count < first + n &&
               static_cast<uint32_t>(extractBits<IpType, address_size>(
                   output.prefix_, output.branch_, ip_prefixes_[new_position + count].ip_)) ==
                   bit_pattern) {
          ++count;
        }

        // This logic was taken from
        // https://github.com/beevek/libkrb/blob/24a224d3ea840e2e7d2926e17d8849aefecc1101/krb/lc_trie.hpp#L396.
        // When there are no entries that match the current pattern, set a leaf at trie_[address +
        // bit_pattern].
        if (count == 0) {
          // This case is hit when the last CIDR range(ip_prefixes_[first+n-1]) is being inserted
          // into the trie_. new_position is decremented by one because the count added to
          // new_position at line 445 are the number of entries already visited.
          if (new_position == first + n) {
            buildRecursive(output.prefix_ + output.branch_, new_position - 1, 1,
                           address + bit_pattern, next_free_index);
          } else {
            buildRecursive(output.prefix_ + output.branch_, new_position, 1, address + bit_pattern,
                           next_free_index);
          }
        } else if (count == 1 &&
                   ip_prefixes_[new_position].length_ - output.prefix_ < output.branch_) {
          // All Ip address that have the prefix of `bit_pattern` will map to the only CIDR range
          // with the bit_pattern as a prefix.
          uint32_t bits = output.branch_ + output.prefix_ - ip_prefixes_[new_position].length_;
          for (uint32_t i = bit_pattern; i < bit_pattern + (1 << bits); ++i) {
            buildRecursive(output.prefix_ + output.branch_, new_position, 1, address + i,
                           next_free_index);
          }
          // Update the bit_pattern to skip over the trie_ entries initialized above.
          bit_pattern += (1 << bits) - 1;
        } else {
          // Recursively build sub-tries for ip_prefixes_[new_position] to
          // ip_prefixes_[new_position+count-1].
          buildRecursive(output.prefix_ + output.branch_, new_position, count,
                         address + bit_pattern, next_free_index);
        }
        new_position += count;
      }
    }

    /**
     * LcNode is a uint32_t. A wrapper is provided to simplify getting/setting the branch, the skip
     * and the address values held within the structure.
     *
     * The LcNode has three parts to it
     * - Branch: the first 5 bits represent the branching factor. The branching factor is used to
     * determine the number of descendants for the current node. The number represents a power of 2,
     * so there can be at most 2^31 descendant nodes.
     * - Skip: the next 7 bits represent the number of bits to skip when looking at an IP address.
     * This value can be between 0 and 127, so IPv6 is supported.
     * - Address: the remaining 20 bits represent an index either into the trie_ or the
     * ip_prefixes_. If branch_ != 0, the index is for the trie_. If branch == zero, the index is
     * for the ip_prefixes_.
     *
     * Note: If more than 2^19-1 CIDR ranges are to be stored in trie_, uint64_t should be used
     * instead.
     */
    struct LcNode {
      uint32_t branch_ : 5;
      uint32_t skip_ : 7;
      uint32_t address_ : 20;
    };

    // Refer to LcNode to for further explanation on the current limitations for the maximum number
    // of CIDR ranges supported and the maximum amount of nodes of supported in the trie.
    static constexpr uint32_t MAXIMUM_CIDR_RANGE_ENTRIES = (1 << 19);

    // During build(), an estimate of the number of nodes required will be made and set this value.
    // This is used to ensure no out_of_range exception is thrown.
    uint32_t maximum_trie_node_size;

    // The CIDR range and tags needs to be maintained separately from the LC-Trie. A LC-Trie skips
    // chunks of data while searching for a match. This means that the node found in the LC-Trie is
    // not guaranteed to have the IP address in range. The last step prior to returning a tag is to
    // check the CIDR range pointed to by the node in the LC-Trie has the IP address in range.
    std::vector<IpPrefix<IpType>> ip_prefixes_;

    // Main trie search structure.
    std::vector<LcNode> trie_;

    const double fill_factor_;
    const uint32_t root_branching_factor_;
  };

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
std::vector<std::string>
LcTrie::LcTrieInternal<IpType, address_size>::getTags(const IpType& ip_address) const {
  std::vector<std::string> return_vector;
  if (trie_.empty()) {
    return return_vector;
  }

  LcNode node = trie_[0];
  uint32_t branch = node.branch_;
  uint32_t position = node.skip_;
  uint32_t address = node.address_;

  // branch == 0 is a leaf node.
  while (branch != 0) {
    // branch is at most 2^5-1= 31 bits to extract, so we can safely cast the
    // output of extractBits to uint32_t without any data loss.
    node = trie_[address + static_cast<uint32_t>(
                               extractBits<IpType, address_size>(position, branch, ip_address))];
    position += branch + node.skip_;
    branch = node.branch_;
    address = node.address_;
  }

  // The path taken through the trie to match the ip_address may have contained skips,
  // so it is necessary to check whether the the matched prefix really contains the
  // ip_address.
  const auto& prefix = ip_prefixes_[address];
  if (prefix.contains(ip_address)) {
    return std::vector<std::string>(prefix.tags_.begin(), prefix.tags_.end());
  }
  return std::vector<std::string>();
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
