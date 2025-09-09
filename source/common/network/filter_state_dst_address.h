#pragma once

#include "envoy/common/hashable.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Overrides the address selection for extensions, e.g. ORIGINAL_DST cluster.
 */
class AddressObject : public Address::InstanceAccessor, public Hashable {
public:
  AddressObject(Network::Address::InstanceConstSharedPtr address)
      : Address::InstanceAccessor(address) {}
  Network::Address::InstanceConstSharedPtr address() const { return getAddress(); }
  absl::optional<std::string> serializeAsString() const override {
    return getAddress() ? absl::make_optional(getAddress()->asString()) : absl::nullopt;
  }
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

  // Implements hashing interface because the value is applied once per upstream connection.
  // Multiple streams sharing the upstream connection must have the same address object.
  absl::optional<uint64_t> hash() const override;
};

/**
 * Registers the filter state object for the dynamic extension support.
 */
class BaseAddressObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override;
};

} // namespace Network
} // namespace Envoy
