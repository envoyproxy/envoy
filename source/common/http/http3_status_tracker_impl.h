#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/alternate_protocols_cache.h"

namespace Envoy {
namespace Http {

class Http3StatusTrackerCallback {
public:
  virtual ~Http3StatusTrackerCallback() = default;
  virtual void onHttp3StatusChanged(const AlternateProtocolsCache::Origin& origin) PURE;
};

// Tracks the status of HTTP/3 being broken for a period of time
// subject to exponential backoff.
class Http3StatusTrackerImpl : public AlternateProtocolsCache::Http3StatusTracker {
public:
  // Returns nullptr if the cached_string is invalid.
  static Http3StatusTrackerSharedPtr
  createFromCachedString(Event::Dispatcher& dispatcher,
                         const AlternateProtocolsCache::Origin& origin,
                         absl::string_view cached_string, Http3StatusTrackerCallback& callback);

  Http3StatusTrackerImpl(Event::Dispatcher& dispatcher,
                         const AlternateProtocolsCache::Origin& origin,
                         Http3StatusTrackerCallback& callback);

  // Returns true if HTTP/3 is broken.
  bool isHttp3Broken() const override;
  // Returns true if HTTP/3 is confirmed to be working.
  bool isHttp3Confirmed() const override;
  // Marks HTTP/3 broken for a period of time, subject to backoff.
  void markHttp3Broken() override;
  // Marks HTTP/3 as confirmed to be working and resets the backoff timeout.
  void markHttp3Confirmed() override;
  // Return strings in form of "state" or "state;expiration_in_sec".
  std::string statusToStringForCache() override;

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
  Event::Dispatcher& dispatcher_;
  const AlternateProtocolsCache::Origin origin_;
  // The number of consecutive times HTTP/3 has been marked broken.
  int consecutive_broken_count_{};
  // The timer which tracks when HTTP/3 broken status should expire
  Event::TimerPtr expiration_timer_;
  MonotonicTime expiration_time_;
  Http3StatusTrackerCallback& callback_;
};

} // namespace Http
} // namespace Envoy
