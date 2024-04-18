#pragma once

#include <functional>
#include <memory>

namespace Envoy {
namespace Event {

/**
 * Callback invoked when a signal event fires.
 */
using SignalCb = std::function<void()>;

/**
 * An abstract signal event. Free the event to stop listening on the signal.
 */
class SignalEvent {
public:
  virtual ~SignalEvent() = default;
};

using SignalEventPtr = std::unique_ptr<SignalEvent>;

} // namespace Event
} // namespace Envoy
