#include "common/http/http3_status_tracker.h"

namespace Envoy {
namespace Http {

namespace {

// Initially, HTTP/3 is be marked broken for 5 minutes.
const std::chrono::minutes DefaultExpirationTime{5};

} // namespace

Http3StatusTracker::Http3StatusTracker(Event::Dispatcher& dispatcher)
    : expiration_timer_(dispatcher.createTimer([this]() -> void { onExpirationTimeout(); })) {}

bool Http3StatusTracker::isHttp3Broken() const { return state_ == State::Broken; }

bool Http3StatusTracker::isHttp3Confirmed() const { return state_ == State::Confirmed; }

void Http3StatusTracker::markHttp3Broken() {
  state_ = State::Broken;
  if (!expiration_timer_->enabled()) {
    expiration_timer_->enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(
        DefaultExpirationTime * (1 << consecutive_broken_count_)));
    ++consecutive_broken_count_;
  }
}

void Http3StatusTracker::markHttp3Confirmed() {
  consecutive_broken_count_ = 0;
  if (expiration_timer_->enabled()) {
    expiration_timer_->disableTimer();
  }
  state_ = State::Confirmed;
}

void Http3StatusTracker::onExpirationTimeout() {
  if (state_ != State::Broken) {
    return;
  }

  state_ = State::Pending;
}

} // namespace Http
} // namespace Envoy
