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
  /**
   * Add an address to the address set.
   * @param address Address to add to the set.
   */
  virtual void add(Network::Address::InstanceConstSharedPtr address) PURE;

  /**
   * Empties the current address set.
   */
  virtual void clear() PURE;

  /**
   * Iterates the address set with the function passed as parameter.
   * @param func Function that will be called with each address from the address set. Note that the
   * iteration will stop when the function returns `false`.
   */
  virtual void
  iterate(const std::function<bool(const Network::Address::InstanceConstSharedPtr& address)>& func)
      const PURE;
};

} // namespace StreamInfo
} // namespace Envoy
