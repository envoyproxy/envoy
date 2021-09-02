#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object that acts as a set/bag.
 */
template <typename T> class SetFilterStateObject : public FilterState::Object {
public:
  /**
   * Add an value to the set.
   * @param value to add to the set.
   */
  virtual void add(const T& value) PURE;

  /**
   * Empties the current set.
   */
  virtual void clear() PURE;

  /**
   * Iterates the set with the function passed as parameter.
   * @param func Function that will be called with each value from the set. Note that the
   * iteration will stop when the function returns `false`.
   */
  virtual void iterate(const std::function<bool(const T& value)>& func) const PURE;
};

} // namespace StreamInfo
} // namespace Envoy
