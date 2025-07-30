#include "source/common/network/radix_tree_ip_list.h"

#include "source/common/network/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Network {
namespace Address {

// static
absl::StatusOr<std::unique_ptr<RadixTreeIpList>> RadixTreeIpList::create(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs) {
  auto ip_list = std::unique_ptr<RadixTreeIpList>(new RadixTreeIpList(cidrs));
  if (!ip_list->error().empty()) {
    return absl::InvalidArgumentError(ip_list->error());
  }
  return ip_list;
}

// static
absl::StatusOr<std::unique_ptr<RadixTreeIpList>>
RadixTreeIpList::create(const Protobuf::RepeatedPtrField<xds::core::v3::CidrRange>& cidrs) {
  auto ip_list = std::unique_ptr<RadixTreeIpList>(new RadixTreeIpList(cidrs));
  if (!ip_list->error().empty()) {
    return absl::InvalidArgumentError(ip_list->error());
  }
  return ip_list;
}

RadixTreeIpList::RadixTreeIpList(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& cidrs) {
  initializeFromCidrs(cidrs);
}

RadixTreeIpList::RadixTreeIpList(
    const Protobuf::RepeatedPtrField<xds::core::v3::CidrRange>& cidrs) {
  initializeFromCidrs(cidrs);
}

template <typename CidrRangeType>
void RadixTreeIpList::initializeFromCidrs(const Protobuf::RepeatedPtrField<CidrRangeType>& cidrs) {
  if (cidrs.empty()) {
    return;
  }

  // Separate IPv4 and IPv6 ranges for optimal trie construction.
  std::vector<std::pair<bool, std::vector<CidrRange>>> ipv4_ranges;
  std::vector<std::pair<bool, std::vector<CidrRange>>> ipv6_ranges;

  ipv4_ranges.emplace_back(std::make_pair(true, std::vector<CidrRange>()));
  ipv6_ranges.emplace_back(std::make_pair(true, std::vector<CidrRange>()));

  for (const auto& entry : cidrs) {
    absl::StatusOr<CidrRange> range_or_error = CidrRange::create(entry);
    if (!range_or_error.status().ok()) {
      error_ = fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                           entry.address_prefix(), entry.prefix_len().value());
      return;
    }

    CidrRange range = std::move(range_or_error.value());
    if (range.ip()->version() == IpVersion::v4) {
      ipv4_ranges[0].second.push_back(std::move(range));
    } else {
      ipv6_ranges[0].second.push_back(std::move(range));
    }
    ip_range_count_++;
  }

  // Create LC Tries for fast IP range lookups.
  // LC Trie constructor handles errors internally and doesn't throw.
  if (!ipv4_ranges[0].second.empty()) {
    ipv4_trie_ = std::make_unique<LcTrie::LcTrie<bool>>(ipv4_ranges);
  }
  if (!ipv6_ranges[0].second.empty()) {
    ipv6_trie_ = std::make_unique<LcTrie::LcTrie<bool>>(ipv6_ranges);
  }
}

bool RadixTreeIpList::contains(const Instance& address) const {
  if (address.type() != Type::Ip) {
    return false;
  }

  // Use StatusOr pattern for proper Envoy error handling.
  const auto result = lookupAddressInTrie(address);
  if (!result.ok()) {
    ENVOY_LOG_MISC(debug, "RadixTreeIpList lookup failed: {}", result.status().message());
    return false;
  }

  return result.value();
}

absl::StatusOr<bool> RadixTreeIpList::lookupAddressInTrie(const Instance& address) const {
  if (address.type() != Type::Ip) {
    return absl::InvalidArgumentError("Address type is not IP");
  }

  // Create a shared_ptr wrapper for the address to match LC Trie interface.
  InstanceConstSharedPtr address_ptr(&address, [](const Instance*) {
    // No-op deleter since we don't own the address.
  });

  if (address.ip()->version() == IpVersion::v4) {
    if (!ipv4_trie_) {
      return absl::FailedPreconditionError("IPv4 trie not initialized");
    }
    auto results = ipv4_trie_->getData(address_ptr);
    return !results.empty();
  } else if (address.ip()->version() == IpVersion::v6) {
    if (!ipv6_trie_) {
      return absl::FailedPreconditionError("IPv6 trie not initialized");
    }
    auto results = ipv6_trie_->getData(address_ptr);
    return !results.empty();
  }

  return absl::InvalidArgumentError(
      fmt::format("Unsupported IP version: {}", static_cast<int>(address.ip()->version())));
}

} // namespace Address
} // namespace Network
} // namespace Envoy
