#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Common {

/**
 * Handle for a callback that can be removed. Destruction of the handle removes the
 * callback.
 */
class CallbackHandle {
public:
  virtual ~CallbackHandle() = default;
};

using CallbackHandlePtr = std::unique_ptr<CallbackHandle>;

} // namespace Common
} // namespace Envoy
