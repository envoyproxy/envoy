#pragma once

#include <functional>
#include <memory>

namespace Envoy {
namespace Event {

/**
 * Callback invoked when a signal event fires.
 */
typedef std::function<void()> SignalCb;

/**
 * An abstract signal event. Free the event to stop listening on the signal.
 */
class SignalEvent {
public:
  virtual ~SignalEvent() {}
};

typedef std::unique_ptr<SignalEvent> SignalEventPtr;

} // namespace Event
} // namespace Envoy
