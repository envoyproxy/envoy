#pragma once

#include "envoy/common/pure.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object that tracks a single uint32_t value.
 */
class UInt32Accessor : public FilterState::Object {
public:
  /**
   * Increments the tracked value by 1.
   */
  virtual void increment() PURE;

  /**
   * @return the tracked value.
   */
  virtual uint32_t value() const PURE;
};

} // namespace StreamInfo
} // namespace Envoy
