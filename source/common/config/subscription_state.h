#pragma once

#include <memory>
#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"

#include "common/config/update_ack.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// Tracks the protocol state of an individual ongoing xDS-over-gRPC session, for a single type_url.
// There can be multiple DeltaSubscriptionStates active, one per type_url. They will all be
// blissfully unaware of each other's existence, even when their messages are being multiplexed
// together by ADS.
// This is the abstract parent class for both the delta and state-of-the-world xDS variants.
class SubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  SubscriptionState(const std::string& type_url, SubscriptionCallbacks& callbacks,
                    const LocalInfo::LocalInfo& local_info,
                    std::chrono::milliseconds init_fetch_timeout, Event::Dispatcher& dispatcher,
                    bool skip_subsequent_node);
  virtual ~SubscriptionState() = default;

  // Update which resources we're interested in subscribing to.
  virtual void updateSubscriptionInterest(const std::set<std::string>& cur_added,
                                          const std::set<std::string>& cur_removed) PURE;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  virtual bool subscriptionUpdatePending() const PURE;

  virtual void markStreamFresh() PURE;

  // Implementations expect either a DeltaDiscoveryResponse or DiscoveryResponse. The caller is
  // expected to know which it should be providing.
  virtual UpdateAck handleResponse(const void* reponse_proto_ptr) PURE;

  virtual void handleEstablishmentFailure() PURE;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual void* getNextRequestAckless() PURE;
  // The WithAck version first calls the Ackless version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller, who is expected to know what type the
  // pointer actually is.
  virtual void* getNextRequestWithAck(const UpdateAck& ack) PURE;

protected:
  void disableInitFetchTimeoutTimer();

  std::string type_url() const { return type_url_; }
  const LocalInfo::LocalInfo& local_info() const { return local_info_; }
  SubscriptionCallbacks& callbacks() const { return callbacks_; }
  bool skip_subsequent_node() const { return skip_subsequent_node_; }

private:
  const std::string type_url_;
  // callbacks_ is expected to be a WatchMap.
  SubscriptionCallbacks& callbacks_;
  const LocalInfo::LocalInfo& local_info_;
  const bool skip_subsequent_node_;
  Event::TimerPtr init_fetch_timeout_timer_;
};

class SubscriptionStateFactory {
public:
  virtual ~SubscriptionStateFactory() = default;
  virtual std::unique_ptr<SubscriptionState>
  makeSubscriptionState(const std::string& type_url, SubscriptionCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout) PURE;
};

} // namespace Config
} // namespace Envoy
