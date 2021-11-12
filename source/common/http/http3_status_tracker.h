#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Http {

// Tracks the status of HTTP/3 being broken for a period of time
// subject to exponential backoff.
class Http3StatusTracker {
public:
  explicit Http3StatusTracker(Event::Dispatcher& dispatcher);

  // Returns true if HTTP/3 is broken.
  bool isHttp3Broken() const;
  // Returns true if HTTP/3 is confirmed to be working.
  bool isHttp3Confirmed() const;
  // Marks HTTP/3 broken for a period of time, subject to backoff.
  void markHttp3Broken();
  // Marks HTTP/3 as confirmed to be working and resets the backoff timeout.
  void markHttp3Confirmed();

private:
  enum class State {
    Pending,
    Broken,
    Confirmed,
  };

  // Called when the expiration timer fires.
  void onExpirationTimeout();

  State state_{State::Pending};
  // The number of consecutive times HTTP/3 has been marked broken.
  int consecutive_broken_count_{};
  // The timer which tracks when HTTP/3 broken status should expire
  Event::TimerPtr expiration_timer_;
};

} // namespace Http
} // namespace Envoy
