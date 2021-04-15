#include "common/http/broken_http3_tracker.h"

namespace Envoy {
namespace Http {

namespace {

// Initially, HTTP/3 is be marked broken for 5 minutes.
std::chrono::minutes DefaultExpirationTime{5};

} // namespace

BrokenHttp3Tracker::BrokenHttp3Tracker(Event::Dispatcher& dispatcher)
    : expiration_timer_(dispatcher.createTimer([this]() -> void { onExpirationTimeout(); })) {}

bool BrokenHttp3Tracker::isHttp3Broken() const { return state_ == State::Broken; }

bool BrokenHttp3Tracker::isHttp3Confirmed() const { return state_ == State::Confirmed; }

void BrokenHttp3Tracker::markHttp3Broken() {
  state_ = State::Broken;
  if (!expiration_timer_->enabled()) {
    expiration_timer_->enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(
        DefaultExpirationTime * (1 << consecutive_broken_count_)));
    ++consecutive_broken_count_;
  }
}

void BrokenHttp3Tracker::markHttp3Confirmed() {
  consecutive_broken_count_ = 0;
  if (expiration_timer_->enabled()) {
    expiration_timer_->disableTimer();
  }
  state_ = State::Confirmed;
}

void BrokenHttp3Tracker::onExpirationTimeout() {
  if (state_ != State::Broken) {
    return;
  }

  state_ = State::Pending;
}

} // namespace Http
} // namespace Envoy
