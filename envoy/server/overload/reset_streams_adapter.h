#pragma once

#include "envoy/common/pure.h"
#include "envoy/server/overload/thread_local_overload_state.h"

namespace Envoy {
namespace Server {

/**
 * Adapts the ratios used in the Overload Manager's ResetStream
 * action translating it to a different quantities.
 */
class ResetStreamAdapter {
public:
  virtual ~ResetStreamAdapter() = default;

  /**
   * Translates the given state to the buckets of streams that should be reset,
   * if any.
   */
  virtual absl::optional<uint32_t> translateToBucketsToReset(OverloadActionState state) const PURE;
};

using ResetStreamAdapterPtr = std::unique_ptr<ResetStreamAdapter>;

} // namespace Server
} // namespace Envoy
