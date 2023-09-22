#pragma once

#include <algorithm>
#include <climits>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/network/address.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/utility.h"

#include "absl/container/node_hash_set.h"
#include "absl/numeric/int128.h"
#include "fmt/format.h"

namespace Envoy {
namespace Network {
namespace LcTrie {

/**
 * Maximum number of nodes an LC trie can hold.
 * @note If the size of LcTrieInternal::LcNode::address_ ever changes, this constant
 *       should be changed to match.
 */
constexpr size_t MaxLcTrieNodes = (1 << 20);

/**
 * Level Compressed Trie for associating data with CIDR ranges. Both IPv4 and IPv6 addresses are
 * supported within this class with no calling pattern changes.
 *
 * The algorithm to build the LC-Trie is described in the paper 'IP-address lookup using LC-tries'
 * by 'S. Nilsson' and 'G. Karlsson'. The paper and reference C implementation can be found here:
 * https://www.nada.kth.se/~snilsson/publications/IP-address-lookup-using-LC-tries/
 *
 * Refer to LcTrieInternal for implementation and algorithm details.
 */
template <class T> class LcTrie {
public:
  /**
   * @param data supplies a vector of data and CIDR ranges.
   * @param exclusive if true then only data for the most specific subnet will be returned
                      (i.e. data isn't inherited from wider ranges).
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
  LcTrie(const std::vector<std::pair<T, std::vector<Address::CidrRange>>>& data,
         bool exclusive = false, double fill_factor = 0.5, uint32_t root_branching_factor = 0) {

    // The LcTrie implementation uses 20-bit "pointers" in its compact internal representation,
    // so it cannot hold more than 2^20 nodes. But the number of nodes can be greater than the
    // number of supported prefixes. Given N prefixes in the data input list, step 2 below can
    // produce a new list of up to 2*N prefixes to insert in the LC trie. And the LC trie can
    // use up to 2*N/fill_factor nodes.
    size_t num_prefixes = 0;
    for (const auto& pair_data : data) {
      num_prefixes += pair_data.second.size();
    }
    const size_t max_prefixes = MaxLcTrieNodes * fill_factor / 2;
    if (num_prefixes > max_prefixes) {
      ExceptionUtil::throwEnvoyException(
          fmt::format("The input vector has '{0}' CIDR range entries. LC-Trie "
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

    BinaryTrie<Ipv4> ipv4_temp(exclusive);
    BinaryTrie<Ipv6> ipv6_temp(exclusive);
    for (const auto& pair_data : data) {
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

    std::vector<IpPrefix<Ipv4>> ipv4_prefixes = ipv4_temp.pushLeaves();
    std::vector<IpPrefix<Ipv6>> ipv6_prefixes = ipv6_temp.pushLeaves();

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
    //    # | branch | skip | first_child | data | note
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

  /**
   * Retrieve data associated with the CIDR range that contains `ip_address`. Both IPv4 and IPv6
   * addresses are supported.
   * @param  ip_address supplies the IP address.
   * @return a vector of data from the CIDR ranges and IP addresses that contains 'ip_address'. An
   * empty vector is returned if no prefix contains 'ip_address' or there is no data for the IP
   * version of the ip_address.
   */
  std::vector<T> getData(const Network::Address::InstanceConstSharedPtr& ip_address) const {
    if (ip_address->ip()->version() == Address::IpVersion::v4) {
      Ipv4 ip = ntohl(ip_address->ip()->ipv4()->address());
      return ipv4_trie_->getData(ip);
    } else {
      Ipv6 ip = Utility::Ip6ntohl(ip_address->ip()->ipv6()->address());
      return ipv6_trie_->getData(ip);
    }
  }

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
    // By shifting the value to the left by p bits(and back), the bits between 0 and p-1 are
    // zero'd out. Then to get the n bits, shift the IP back by the address_size minus the number
    // of desired bits.
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
  using Ipv4 = uint32_t;
  using Ipv6 = absl::uint128;

  using DataSet = absl::node_hash_set<T>;
  using DataSetSharedPtr = std::shared_ptr<DataSet>;

  /**
   * Structure to hold a CIDR range and the data associated with it.
   */
  template <class IpType, uint32_t address_size = CHAR_BIT * sizeof(IpType)> struct IpPrefix {

    IpPrefix() = default;

    IpPrefix(const IpType& ip, uint32_t length, const T& data) : ip_(ip), length_(length) {
      data_.insert(data);
    }

    IpPrefix(const IpType& ip, int length, const DataSet& data)
        : ip_(ip), length_(length), data_(data) {}

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

    // The address represented either in Ipv4(uint32_t) or Ipv6(absl::uint128).
    IpType ip_{0};
    // Length of the cidr range.
    uint32_t length_{0};
    // Data for this entry.
    DataSet data_;
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
    BinaryTrie(bool exclusive) : root_(std::make_unique<Node>()), exclusive_(exclusive) {}

    /**
     * Add a CIDR prefix and associated data to the binary trie. If an entry already
     * exists for the prefix, merge the data into the existing entry.
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
      if (node->data == nullptr) {
        node->data = std::make_shared<DataSet>();
      }
      node->data->insert(prefix.data_.begin(), prefix.data_.end());
    }

    /**
     * Update each node in the trie to inherit/override its ancestors' data,
     * and then push the prefixes in the binary trie to the leaves so that:
     *  1) each leaf contains a prefix, and
     *  2) given the set of prefixes now located at the leaves, a useful
     *     new property applies: no prefix in that set is nested under any
     *     other prefix in the set (since, by definition, no leaf of the
     *     trie can be nested under another leaf)
     * @return the prefixes associated with the leaf nodes.
     */
    std::vector<IpPrefix<IpType>> pushLeaves() {
      std::vector<IpPrefix<IpType>> prefixes;
      std::function<void(Node*, DataSetSharedPtr, unsigned, IpType)> visit =
          [&](Node* node, DataSetSharedPtr data, unsigned depth, IpType prefix) {
            // Inherit any data set by ancestor nodes.
            if (data != nullptr) {
              if (node->data == nullptr) {
                node->data = data;
              } else if (!exclusive_) {
                node->data->insert(data->begin(), data->end());
              }
            }
            // If a node has exactly one child, create a second child node
            // that inherits the union of all data set by any ancestor nodes.
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
              visit(node->children[0].get(), node->data, depth + 1, (prefix << 1) + IpType(0));
              visit(node->children[1].get(), node->data, depth + 1, (prefix << 1) + IpType(1));
            } else {
              if (node->data != nullptr) {
                // Compute the CIDR prefix from the path we've taken to get to this point in the
                // tree.
                IpType ip = prefix;
                if (depth != 0) {
                  ip <<= (address_size - depth);
                }
                prefixes.emplace_back(IpPrefix<IpType>(ip, depth, *node->data));
              }
            }
          };
      visit(root_.get(), nullptr, 0, IpType(0));
      return prefixes;
    }

  private:
    struct Node {
      std::unique_ptr<Node> children[2];
      DataSetSharedPtr data;
    };
    using NodePtr = std::unique_ptr<Node>;
    NodePtr root_;
    bool exclusive_;
  };

  /**
   * Level Compressed Trie (LC-Trie) that contains CIDR ranges and its corresponding data.
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
     * @param data supplies a vector of data and CIDR ranges (in IpPrefix format).
     * @param fill_factor supplies the fraction of completeness to use when calculating the branch
     *                    value for a sub-trie.
     * @param root_branching_factor supplies the branching factor at the root. The paper suggests
     *                              for large LC-Tries to use the value '16' for the root
     *                              branching factor. It reduces the depth of the trie.
     */
    LcTrieInternal(std::vector<IpPrefix<IpType>>& data, double fill_factor,
                   uint32_t root_branching_factor);

    /**
     * Retrieve the data associated with the CIDR range that contains `ip_address`.
     * @param  ip_address supplies the IP address in host byte order.
     * @return a vector of data from the CIDR ranges and IP addresses that encompasses the input.
     * An empty vector is returned if the LC Trie is empty.
     */
    std::vector<T> getData(const IpType& ip_address) const;

  private:
    /**
     * Builds the Level Compressed Trie, by first sorting the data, removing duplicated
     * prefixes and invoking buildRecursive() to build the trie.
     */
    void build(std::vector<IpPrefix<IpType>>& data) {
      if (data.empty()) {
        return;
      }

      ip_prefixes_ = data;
      std::sort(ip_prefixes_.begin(), ip_prefixes_.end());

      // Build the trie_.
      trie_.reserve(static_cast<size_t>(ip_prefixes_.size() / fill_factor_));
      uint32_t next_free_index = 1;
      buildRecursive(0u, 0u, ip_prefixes_.size(), 0u, next_free_index);

      // The value of next_free_index is the final size of the trie_.
      ASSERT(next_free_index <= trie_.size());
      trie_.resize(next_free_index);
      trie_.shrink_to_fit();
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
     * @param n supplies the number of entries for the sub-trie.
     * @param position supplies the root for this sub-trie.
     * @param next_free_index supplies the next available index in the trie_.
     */
    void buildRecursive(uint32_t prefix, uint32_t first, uint32_t n, uint32_t position,
                        uint32_t& next_free_index) {
      if (position >= trie_.size()) {
        // There is no way to predictably determine the number of trie nodes required to build a
        // LC-Trie. If while building the trie the position that is being set exceeds the maximum
        // number of supported trie_ entries, throw an Envoy Exception.
        if (position >= MaxLcTrieNodes) {
          // Adding 1 to the position to count how many nodes are trying to be set.
          ExceptionUtil::throwEnvoyException(
              fmt::format("The number of internal nodes required for the LC-Trie "
                          "exceeded the maximum number of "
                          "supported nodes. Minimum number of internal nodes required: "
                          "'{0}'. Maximum number of supported nodes: '{1}'.",
                          (position + 1), MaxLcTrieNodes));
        }
        trie_.resize(position + 1);
      }
      // Setting a leaf, the branch and skip are 0.
      if (n == 1) {
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

      // The next available free index to populate in the trie_ is at next_free_index +
      // 2^(branching factor).
      next_free_index += 1 << output.branch_;

      uint32_t new_position = first;

      // Build the subtrees.
      for (uint32_t bit_pattern = 0; bit_pattern < static_cast<uint32_t>(1 << output.branch_);
           ++bit_pattern) {

        // count is the number of entries in the ip_prefixes_ vector that have the same bit
        // pattern as the ip_prefixes_[new_position].
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
     * LcNode is a uint32_t. A wrapper is provided to simplify getting/setting the branch, the
     * skip and the address values held within the structure.
     *
     * The LcNode has three parts to it
     * - Branch: the first 5 bits represent the branching factor. The branching factor is used to
     * determine the number of descendants for the current node. The number represents a power of
     * 2, so there can be at most 2^31 descendant nodes.
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
      uint32_t address_ : 20; // If this 20-bit size changes, please change MaxLcTrieNodes too.
    };

    // The CIDR range and data needs to be maintained separately from the LC-Trie. A LC-Trie skips
    // chunks of data while searching for a match. This means that the node found in the LC-Trie
    // is not guaranteed to have the IP address in range. The last step prior to returning
    // associated data is to check the CIDR range pointed to by the node in the LC-Trie has
    // the IP address in range.
    std::vector<IpPrefix<IpType>> ip_prefixes_;

    // Main trie search structure.
    std::vector<LcNode> trie_;

    const double fill_factor_;
    const uint32_t root_branching_factor_;
  };

  std::unique_ptr<LcTrieInternal<Ipv4>> ipv4_trie_;
  std::unique_ptr<LcTrieInternal<Ipv6>> ipv6_trie_;
};

template <class T>
template <class IpType, uint32_t address_size>
LcTrie<T>::LcTrieInternal<IpType, address_size>::LcTrieInternal(std::vector<IpPrefix<IpType>>& data,
                                                                double fill_factor,
                                                                uint32_t root_branching_factor)
    : fill_factor_(fill_factor), root_branching_factor_(root_branching_factor) {
  build(data);
}

template <class T>
template <class IpType, uint32_t address_size>
std::vector<T>
LcTrie<T>::LcTrieInternal<IpType, address_size>::getData(const IpType& ip_address) const {
  std::vector<T> return_vector;
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
  // so it is necessary to check whether the matched prefix really contains the
  // ip_address.
  const auto& prefix = ip_prefixes_[address];
  if (prefix.contains(ip_address)) {
    return std::vector<T>(prefix.data_.begin(), prefix.data_.end());
  }
  return std::vector<T>();
}

} // namespace LcTrie
} // namespace Network
} // namespace Envoy
