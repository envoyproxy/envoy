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
  // Note that, outside of tests, we expect callbacks to always be a WatchMap.
  SotwSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout,
                        Event::Dispatcher& dispatcher);
  ~SotwSubscriptionState() override;

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const std::set<std::string>& cur_added,
                                  const std::set<std::string>& cur_removed) override;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const override;

  void markStreamFresh() override;

  // message is expected to be a envoy::service::discovery::v3::DiscoveryResponse.
  UpdateAck handleResponse(const void* response_proto_ptr) override;

  void handleEstablishmentFailure() override;

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  // Returns a new'd pointer, meant to be owned by the caller.
  void* getNextRequestAckless() override;
  // The WithAck version first calls the Ackless version, then adds in the passed-in ack.
  // Returns a new'd pointer, meant to be owned by the caller.
  void* getNextRequestWithAck(const UpdateAck& ack) override;

  void ttlExpiryCallback(const std::vector<std::string>& expired) override;

  SotwSubscriptionState(const SotwSubscriptionState&) = delete;
  SotwSubscriptionState& operator=(const SotwSubscriptionState&) = delete;

private:
  // Returns a new'd pointer, meant to be owned by the caller.
  envoy::service::discovery::v3::DiscoveryRequest* getNextRequestInternal();

  void handleGoodResponse(const envoy::service::discovery::v3::DiscoveryResponse& message);
  void handleBadResponse(const EnvoyException& e, UpdateAck& ack);

  bool isHeartbeatResource(const envoy::service::discovery::v3::Resource& resource,
                           const std::string& version);
  void setResourceTtl(const envoy::service::discovery::v3::Resource& resource);

  // The version_info carried by the last accepted DiscoveryResponse.
  // Remains empty until one is accepted.
  absl::optional<std::string> last_good_version_info_;
  // The nonce carried by the last accepted DiscoveryResponse.
  // Remains empty until one is accepted.
  // Used when it's time to make a spontaneous (i.e. not primarily meant as an ACK) request.
  absl::optional<std::string> last_good_nonce_;

  // Starts true because we should send a request upon subscription start.
  bool update_pending_{true};

  absl::flat_hash_set<std::string> names_tracked_;
};

class SotwSubscriptionStateFactory : public SubscriptionStateFactory {
public:
  SotwSubscriptionStateFactory(Event::Dispatcher& dispatcher);
  ~SotwSubscriptionStateFactory() override;
  std::unique_ptr<SubscriptionState>
  makeSubscriptionState(const std::string& type_url, UntypedConfigUpdateCallbacks& callbacks,
                        std::chrono::milliseconds init_fetch_timeout) override;

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace Config
} // namespace Envoy
