#pragma once

#include <memory>
#include <vector>

#include "envoy/network/address.h"

#include "source/common/common/radix_tree.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Network {
namespace Address {

/**
 * High-performance IP address range matcher using RadixTree for O(log n) lookups.
 * This class provides significant performance improvements over linear search
 * for large IP range lists commonly used in RBAC policies.
 */
class RadixTreeIpList {
public:
  static absl::StatusOr<std::unique_ptr<RadixTreeIpList>>
  create(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs);
  static absl::StatusOr<std::unique_ptr<RadixTreeIpList>>
  create(const Protobuf::RepeatedPtrField<xds::core::v3::CidrRange>& cidrs);

  RadixTreeIpList() = default;

  /**
   * Check if the given IP address is contained in any of the CIDR ranges.
   * Performance: O(log n) average case vs O(n) linear search.
   * @param address the IP address to check.
   * @return true if the address is in any range, false otherwise.
   */
  bool contains(const Instance& address) const;

  /**
   * Get the number of IP ranges in this list.
   * @return the size of the IP range list.
   */
  size_t getIpListSize() const { return ip_range_count_; }

  /**
   * Get any error message from initialization.
   * @return the error string, empty if no error.
   */
  const std::string& error() const { return error_; }

private:
  RadixTreeIpList(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs);
  RadixTreeIpList(const Protobuf::RepeatedPtrField<xds::core::v3::CidrRange>& cidrs);

  template <typename CidrRangeType>
  void initializeFromCidrs(const Protobuf::RepeatedPtrField<CidrRangeType>& cidrs);

  // Helper method for StatusOr-based address lookup.
  absl::StatusOr<bool> lookupAddressInTrie(const Instance& address) const;

  // LC Trie provides optimal performance for IP address range matching.
  // We use a simple boolean value since we only care about membership.
  std::unique_ptr<LcTrie::LcTrie<bool>> ipv4_trie_;
  std::unique_ptr<LcTrie::LcTrie<bool>> ipv6_trie_;

  size_t ip_range_count_{0};
  std::string error_;
};

} // namespace Address
} // namespace Network
} // namespace Envoy
