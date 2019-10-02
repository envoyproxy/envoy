#include "common/config/subscription_state.h"

#include <memory>
#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"

namespace Envoy {
namespace Config {

SubscriptionState::SubscriptionState(std::string type_url, SubscriptionCallbacks& callbacks,
                                     std::chrono::milliseconds init_fetch_timeout,
                                     Event::Dispatcher& dispatcher)
    : type_url_(std::move(type_url)), callbacks_(callbacks) {
  if (init_fetch_timeout.count() > 0 && !init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_ = dispatcher.createTimer([this]() -> void {
      ENVOY_LOG(warn, "config: initial fetch timed out for {}", type_url_);
      callbacks_.onConfigUpdateFailed(ConfigUpdateFailureReason::FetchTimedout, nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout);
  }
}

void SubscriptionState::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

} // namespace Config
} // namespace Envoy
