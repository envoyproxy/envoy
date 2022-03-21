#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/alternate_protocols_cache.h"

namespace Envoy {
namespace Http {

// Tracks the status of HTTP/3 being broken for a period of time
// subject to exponential backoff.
class Http3StatusTrackerImpl : public AlternateProtocolsCache::Http3StatusTracker {
public:
  explicit Http3StatusTrackerImpl(Event::Dispatcher& dispatcher);

  // Returns true if HTTP/3 is broken.
  bool isHttp3Broken() const override;
  // Returns true if HTTP/3 is confirmed to be working.
  bool isHttp3Confirmed() const override;
  // Marks HTTP/3 broken for a period of time, subject to backoff.
  void markHttp3Broken() override;
  // Marks HTTP/3 as confirmed to be working and resets the backoff timeout.
  void markHttp3Confirmed() override;

private:
  enum class State {
    Pending,
    Broken,
    Confirmed,
  };

  std::string stateToString(State s) {
    switch (s) {
    case State::Pending:
      return "Pending";
    case State::Broken:
      return "Broken";
    case State::Confirmed:
      return "Confirmed";
    }
    IS_ENVOY_BUG("Unexpected HTTP3 status");
    return "";
  }

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
