#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/grpc/status.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/config/subscription_state.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

// Tracks the state of a "state-of-the-world" (i.e. not delta) xDS-over-gRPC protocol session.
class SotwSubscriptionState : public SubscriptionState {
public:
  SotwSubscriptionState(const std::string& type_url, SubscriptionCallbacks& callbacks,
                        const LocalInfo::LocalInfo& local_info,
                        std::chrono::milliseconds init_fetch_timeout, Event::Dispatcher& dispatcher,
                        bool skip_subsequent_node);
  ~SotwSubscriptionState() override;

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const std::set<std::string>& cur_added,
                                  const std::set<std::string>& cur_removed) override;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const override;

  void markStreamFresh() override { any_request_sent_yet_in_current_stream_ = false; }

  // message is expected to be a envoy::api::v2::DiscoveryResponse.
  UpdateAck handleResponse(const void* reponse_proto_ptr) override;

  void handleEstablishmentFailure() override;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller.
  void* getNextRequestAckless() override;
  // The WithAck version first calls the Ackless version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller.
  void* getNextRequestWithAck(const UpdateAck& ack) override;

private:
  // Returns a new'd pointer, meant to be owned by the caller.
  envoy::api::v2::DiscoveryRequest* getNextRequestInternal();

  void handleGoodResponse(const envoy::api::v2::DiscoveryResponse& message);
  void handleBadResponse(const EnvoyException& e, UpdateAck& ack);

  // The version_info value most recently received in a DiscoveryResponse that was accepted.
  // Remains empty until one is accepted.
  absl::optional<std::string> last_good_version_info_;

  bool any_request_sent_yet_in_current_stream_{};
  // Starts true because we should send a request upon subscription start.
  bool update_pending_{true};

  absl::flat_hash_set<std::string> names_tracked_;

  SotwSubscriptionState(const SotwSubscriptionState&) = delete;
  SotwSubscriptionState& operator=(const SotwSubscriptionState&) = delete;
};

class SotwSubscriptionStateFactory : public SubscriptionStateFactory {
public:
  SotwSubscriptionStateFactory(Event::Dispatcher& dispatcher,
                               const LocalInfo::LocalInfo& local_info, bool skip_subsequent_node);
  ~SotwSubscriptionStateFactory() override;
  std::unique_ptr<SubscriptionState>
  makeSubscriptionState(const std::string& type_url, SubscriptionCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout) override;

private:
  Event::Dispatcher& dispatcher_;
  const LocalInfo::LocalInfo& local_info_;
  const bool skip_subsequent_node_;
};

} // namespace Config
} // namespace Envoy
