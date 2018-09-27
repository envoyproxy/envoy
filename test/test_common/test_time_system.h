#pragma once

#include "envoy/event/timer.h"

namespace Envoy {
namespace Event {

// Adds sleep() interface to Event::TimeSystem.
class TestTimeSystem : public Event::TimeSystem {
public:
  /**
   * Advances time forward by the specified duration, running any timers
   * along the way that have been scheduled to fire.
   *
   * @param duration The amount of time to sleep.
   */
  virtual void sleep(const Duration& duration) PURE;
  template <class D> void sleep(const D& duration) {
    sleep(std::chrono::duration_cast<Duration>(duration));
  }
};

} // namespace Event
} // namespace Envoy
