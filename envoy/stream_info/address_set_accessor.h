#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object that holds a set of network addresses.
 */
class AddressSetAccessor : public FilterState::Object {
public:
  virtual void add(Network::Address::InstanceConstSharedPtr address) PURE;

  virtual void
  iterate(const std::function<bool(const Network::Address::InstanceConstSharedPtr& address)>&)
      const PURE;
};

} // namespace StreamInfo
} // namespace Envoy
