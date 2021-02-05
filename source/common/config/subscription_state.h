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

// Tracks the protocol state of an individual ongoing xDS-over-gRPC session, for a single type_url.
// There can be multiple SubscriptionStates active, one per type_url. They will all be
// blissfully unaware of each other's existence, even when their messages are being multiplexed
// together by ADS.
// This is the abstract parent class for both the delta and state-of-the-world xDS variants.
class SubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  SubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                    std::chrono::milliseconds init_fetch_timeout, Event::Dispatcher& dispatcher);
  virtual ~SubscriptionState() = default;

  // Update which resources we're interested in subscribing to.
  virtual void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                          const absl::flat_hash_set<std::string>& cur_removed) PURE;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  // Implementations expect either a DeltaDiscoveryResponse or DiscoveryResponse. The caller is
  // expected to know which it should be providing.
  virtual UpdateAck handleResponse(const void* response_proto_ptr) PURE;

  virtual void handleEstablishmentFailure() PURE;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual void* getNextRequestAckless() PURE;
  // The WithAck version first calls the ack-less version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual void* getNextRequestWithAck(const UpdateAck& ack) PURE;

  void disableInitFetchTimeoutTimer();

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
};

class SubscriptionStateFactory {
public:
  virtual ~SubscriptionStateFactory() = default;
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  virtual std::unique_ptr<SubscriptionState>
  makeSubscriptionState(const std::string& type_url, UntypedConfigUpdateCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout) PURE;
};

} // namespace Config
} // namespace Envoy
