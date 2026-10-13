#pragma once

#include "envoy/common/pure.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object that tracks a single boolean value.
 */
class BoolAccessor : public FilterState::Object {
public:
  /**
   * @return the tracked value.
   */
  virtual bool value() const PURE;
};

} // namespace StreamInfo
} // namespace Envoy
