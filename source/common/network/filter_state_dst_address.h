#pragma once

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * Overrides the destination host address selection for ORIGINAL_DST cluster.
 */
class DestinationAddress : public StreamInfo::FilterState::Object {
public:
  // Returns the key for looking up in the FilterState.
  static const std::string& key();

  DestinationAddress(Network::Address::InstanceConstSharedPtr address) : address_(address) {}
  Network::Address::InstanceConstSharedPtr address() const { return address_; }

private:
  const Network::Address::InstanceConstSharedPtr address_;
};

} // namespace Network
} // namespace Envoy
