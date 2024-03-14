#include "source/common/http/http3_status_tracker_impl.h"

namespace Envoy {
namespace Http {

namespace {

// Initially, HTTP/3 is marked broken for 1 second.
const std::chrono::seconds DefaultExpirationTime{1};
// Cap the broken period around a day and a half.
const int MaxConsecutiveBrokenCount = 17;
} // namespace

Http3StatusTrackerImpl::Http3StatusTrackerImpl(Event::Dispatcher& dispatcher)
    : expiration_timer_(dispatcher.createTimer([this]() -> void { onExpirationTimeout(); })) {}

bool Http3StatusTrackerImpl::isHttp3Broken() const { return state_ == State::Broken; }

bool Http3StatusTrackerImpl::isHttp3Confirmed() const { return state_ == State::Confirmed; }

bool Http3StatusTrackerImpl::hasHttp3FailedRecently() const {
  return state_ == State::FailedRecently;
}

void Http3StatusTrackerImpl::markHttp3Broken() {
  state_ = State::Broken;
  if (!expiration_timer_->enabled()) {
    std::chrono::seconds expiration_in_sec =
        DefaultExpirationTime * (1 << consecutive_broken_count_);
    expiration_timer_->enableTimer(
        std::chrono::duration_cast<std::chrono::milliseconds>(expiration_in_sec));
    if (consecutive_broken_count_ < MaxConsecutiveBrokenCount) {
      ++consecutive_broken_count_;
    }
  }
}

void Http3StatusTrackerImpl::markHttp3Confirmed() {
  state_ = State::Confirmed;
  consecutive_broken_count_ = 0;
  if (expiration_timer_->enabled()) {
    expiration_timer_->disableTimer();
  }
}

void Http3StatusTrackerImpl::markHttp3FailedRecently() { state_ = State::FailedRecently; }

void Http3StatusTrackerImpl::onExpirationTimeout() {
  if (state_ != State::Broken) {
    return;
  }
  state_ = State::FailedRecently;
}

} // namespace Http
} // namespace Envoy
