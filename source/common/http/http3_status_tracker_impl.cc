#include "source/common/http/http3_status_tracker_impl.h"

#include <chrono>
#include <functional>

namespace Envoy {
namespace Http {

namespace {

// Initially, HTTP/3 is be marked broken for 5 minutes.
const std::chrono::minutes DefaultExpirationTime{5};
// Cap the broken period at just under 1 day.
const int MaxConsecutiveBrokenCount = 8;
} // namespace

Http3StatusTrackerSharedPtr Http3StatusTrackerImpl::createFromCachedString(
    Event::Dispatcher& dispatcher, const AlternateProtocolsCache::Origin& origin,
    absl::string_view cached_string, Http3StatusTrackerCallback& callback) {
  const std::vector<absl::string_view> parts = absl::StrSplit(cached_string, ';');
  if (parts.size() > 2 || parts.empty()) {
    return nullptr;
  }
  absl::string_view state_str = parts[0];
  State state;
  if (state_str == "Broken") {
    state = State::Broken;
  } else if (state_str == "Confirmed") {
    state = State::Confirmed;
  } else if (state_str == "Pending") {
    state = State::Pending;
  } else {
    ENVOY_LOG_MISC(warn, "Unexpected state {} from cache.", state_str);
    return nullptr;
  }

  auto tracker = std::make_shared<Http3StatusTrackerImpl>(dispatcher, origin, callback);
  tracker->state_ = state;
  if (state == State::Broken) {
    if (parts.size() == 1u) {
      ENVOY_LOG_MISC(warn, "HTTP/3 is broken without expiration.");
      return nullptr;
    }
    absl::string_view expiration_str = parts[1];
    int64_t expiration_time;
    if (!absl::SimpleAtoi(expiration_str, &expiration_time)) {
      ENVOY_LOG_MISC(warn, "Unexpected expiration {} from cache.", expiration_str);
      return nullptr;
    }
    auto expire_time_from_epoch = std::chrono::seconds(expiration_time);
    auto time_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        dispatcher.approximateMonotonicTime().time_since_epoch());
    if (expire_time_from_epoch < time_since_epoch) {
      ENVOY_LOG_MISC(trace, "HTTP/3 status is already expired, reset the state.");
      tracker->onExpirationTimeout();
    } else {
      if (tracker->consecutive_broken_count_ < MaxConsecutiveBrokenCount) {
        ++tracker->consecutive_broken_count_;
      }
      std::chrono::milliseconds expiration = expire_time_from_epoch - time_since_epoch;
      tracker->expiration_timer_->enableTimer(expiration);
      tracker->expiration_time_ = dispatcher.approximateMonotonicTime() + expiration;
    }
  }
  return tracker;
}

Http3StatusTrackerImpl::Http3StatusTrackerImpl(Event::Dispatcher& dispatcher,
                                               const AlternateProtocolsCache::Origin& origin,
                                               Http3StatusTrackerCallback& callback)
    : dispatcher_(dispatcher), origin_(origin),
      expiration_timer_(dispatcher.createTimer([this]() -> void { onExpirationTimeout(); })),
      callback_(callback) {}

bool Http3StatusTrackerImpl::isHttp3Broken() const { return state_ == State::Broken; }

bool Http3StatusTrackerImpl::isHttp3Confirmed() const { return state_ == State::Confirmed; }

void Http3StatusTrackerImpl::markHttp3Broken() {
  state_ = State::Broken;
  if (!expiration_timer_->enabled()) {
    std::chrono::minutes expiration_in_min =
        DefaultExpirationTime * (1 << consecutive_broken_count_);
    expiration_timer_->enableTimer(
        std::chrono::duration_cast<std::chrono::milliseconds>(expiration_in_min));
    if (consecutive_broken_count_ < MaxConsecutiveBrokenCount) {
      ++consecutive_broken_count_;
    }
    expiration_time_ = expiration_in_min + dispatcher_.approximateMonotonicTime();
    callback_.onHttp3StatusChanged(origin_);
  }
}

void Http3StatusTrackerImpl::markHttp3Confirmed() {
  const bool state_changed = (state_ != State::Confirmed);
  state_ = State::Confirmed;
  consecutive_broken_count_ = 0;
  if (expiration_timer_->enabled()) {
    expiration_timer_->disableTimer();
  }
  if (state_changed) {
    callback_.onHttp3StatusChanged(origin_);
  }
}

void Http3StatusTrackerImpl::onExpirationTimeout() {
  if (state_ != State::Broken) {
    return;
  }
  state_ = State::Pending;
  callback_.onHttp3StatusChanged(origin_);
}

std::string Http3StatusTrackerImpl::statusToStringForCache() {
  std::string str = stateToString(state_);
  if (state_ == State::Broken) {
    absl::StrAppend(
        &str, ";",
        std::chrono::duration_cast<std::chrono::seconds>(expiration_time_.time_since_epoch())
            .count());
  }
  return str;
}

} // namespace Http
} // namespace Envoy
