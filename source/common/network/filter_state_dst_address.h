#pragma once

#include "envoy/common/hashable.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Overrides the address selection for extensions, e.g. ORIGINAL_DST cluster.
 */
class AddressObject : public StreamInfo::FilterState::Object, public Hashable {
public:
  AddressObject(Network::Address::InstanceConstSharedPtr address) : address_(address) {}
  Network::Address::InstanceConstSharedPtr address() const { return address_; }
  absl::optional<std::string> serializeAsString() const override {
    return address_ ? absl::make_optional(address_->asString()) : absl::nullopt;
  }
  // Implements hashing interface because the value is applied once per upstream connection.
  // Multiple streams sharing the upstream connection must have the same address object.
  absl::optional<uint64_t> hash() const override;

private:
  const Network::Address::InstanceConstSharedPtr address_;
  friend class AddressObjectReflection;
};

/**
 * Registers the filter state object for the dynamic extension support.
 */
class BaseAddressObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override;
  std::unique_ptr<StreamInfo::FilterState::ObjectReflection>
  reflect(const StreamInfo::FilterState::Object* data) const override;
};

} // namespace Network
} // namespace Envoy
