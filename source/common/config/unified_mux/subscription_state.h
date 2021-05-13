#pragma once

#include <memory>
#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "common/config/ttl.h"
#include "common/config/update_ack.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {
namespace UnifiedMux {

// Tracks the protocol state of an individual ongoing xDS-over-gRPC session, for a single type_url.
// There can be multiple SubscriptionStates active, one per type_url. They will all be
// blissfully unaware of each other's existence, even when their messages are being multiplexed
// together by ADS.
// This is the abstract parent class for both the delta and state-of-the-world xDS variants.
template <class RS, class RQ>
class SubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  SubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                    std::chrono::milliseconds init_fetch_timeout, Event::Dispatcher& dispatcher)
      // TODO(snowp): Hard coding VHDS here is temporary until we can move it away from relying on
      // empty resources as updates.
      : supports_heartbeats_(type_url != "envoy.config.route.v3.VirtualHost"),
        ttl_([this](const std::vector<std::string>& expired) { ttlExpiryCallback(expired); },
             dispatcher, dispatcher.timeSource()),
        type_url_(std::move(type_url)), callbacks_(callbacks), dispatcher_(dispatcher) {
    if (init_fetch_timeout.count() > 0 && !init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_ = dispatcher.createTimer([this]() -> void {
        ENVOY_LOG(warn, "config: initial fetch timed out for {}", type_url_);
        callbacks_.onConfigUpdateFailed(ConfigUpdateFailureReason::FetchTimedout, nullptr);
      });
      init_fetch_timeout_timer_->enableTimer(init_fetch_timeout);
    }
  }

  virtual ~SubscriptionState() = default;

  // Update which resources we're interested in subscribing to.
  virtual void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                          const absl::flat_hash_set<std::string>& cur_removed) PURE;

  void setDynamicContextChanged() { dynamic_context_changed_ = true; }
  void clearDynamicContextChanged() { dynamic_context_changed_ = false; }
  bool dynamicContextChanged() const { return dynamic_context_changed_; }

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  // Implementations expect either a DeltaDiscoveryResponse or DiscoveryResponse. The caller is
  // expected to know which it should be providing.
  virtual UpdateAck handleResponse(const RS& response_proto_ptr) PURE;

  virtual void handleEstablishmentFailure() PURE;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual RQ* getNextRequestAckless() PURE;
  // The WithAck version first calls the ack-less version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual RQ* getNextRequestWithAck(const UpdateAck& ack) PURE;

  void disableInitFetchTimeoutTimer() {
    if (init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_->disableTimer();
      init_fetch_timeout_timer_.reset();
    }
  }

  virtual void ttlExpiryCallback(const std::vector<std::string>& type_url) PURE;

protected:
  std::string type_url() const { return type_url_; }
  UntypedConfigUpdateCallbacks& callbacks() const { return callbacks_; }

  // Not all xDS resources supports heartbeats due to there being specific information encoded in
  // an empty response, which is indistinguishable from a heartbeat in some cases. For now we just
  // disable heartbeats for these resources (currently only VHDS).
  const bool supports_heartbeats_;
  TtlManager ttl_;
  const std::string type_url_;
  // callbacks_ is expected (outside of tests) to be a WatchMap.
  UntypedConfigUpdateCallbacks& callbacks_;
  Event::Dispatcher& dispatcher_;
  Event::TimerPtr init_fetch_timeout_timer_;
  bool dynamic_context_changed_{};
};

} // namespace UnifiedMux
} // namespace Config
} // namespace Envoy
