#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/api_version.h"
#include "source/common/config/pausable_ack_queue.h"
#include "source/common/config/ttl.h"
#include "source/common/config/watch_map.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {

// Tracks the xDS protocol state of an individual ongoing delta xDS session, i.e. a single type_url.
// There can be multiple DeltaSubscriptionStates active. They will always all be
// blissfully unaware of each other's existence, even when their messages are
// being multiplexed together by ADS.
class DeltaSubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& watch_map,
                         const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher);

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed);
  void addAliasesToResolve(const absl::flat_hash_set<std::string>& aliases);
  void setMustSendDiscoveryRequest() { must_send_discovery_request_ = true; }

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const;

  void markStreamFresh() { any_request_sent_yet_in_current_stream_ = false; }

  UpdateAck handleResponse(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message);

  void handleEstablishmentFailure();

  // Returns the next gRPC request proto to be sent off to the server, based on this object's
  // understanding of the current protocol state, and new resources that Envoy wants to request.
  envoy::service::discovery::v3::DeltaDiscoveryRequest getNextRequestAckless();

  // The WithAck version first calls the Ack-less version, then adds in the passed-in ack.
  envoy::service::discovery::v3::DeltaDiscoveryRequest getNextRequestWithAck(const UpdateAck& ack);

  DeltaSubscriptionState(const DeltaSubscriptionState&) = delete;
  DeltaSubscriptionState& operator=(const DeltaSubscriptionState&) = delete;

private:
  bool isHeartbeatResponse(const envoy::service::discovery::v3::Resource& resource) const;
  void handleGoodResponse(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message);
  void handleBadResponse(const EnvoyException& e, UpdateAck& ack);

  class ResourceState {
  public:
    ResourceState(absl::optional<std::string> version) : version_(std::move(version)) {}

    ResourceState(const envoy::service::discovery::v3::Resource& resource)
        : ResourceState(resource.version()) {}

    // Builds a ResourceState in the waitingForServer state.
    ResourceState() : ResourceState(absl::nullopt) {}

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool waitingForServer() const { return version_ == absl::nullopt; }

    void setAsWaitingForServer() { version_ = absl::nullopt; }

    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
  };

  void addResourceStateFromServer(const envoy::service::discovery::v3::Resource& resource);
  OptRef<ResourceState> getRequestedResourceState(const std::string& resource_name);

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  absl::node_hash_map<std::string, ResourceState> requested_resource_state_;
  // A map from resource name to per-resource version. The keys of this map are resource names we
  // have received as a part of the wildcard subscription.
  absl::node_hash_map<std::string, std::string> wildcard_resource_state_;

  // Not all xDS resources supports heartbeats due to there being specific information encoded in
  // an empty response, which is indistinguishable from a heartbeat in some cases. For now we just
  // disable heartbeats for these resources (currently only VHDS).
  const bool supports_heartbeats_;
  TtlManager ttl_;

  const std::string type_url_;
  UntypedConfigUpdateCallbacks& watch_map_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;

  bool any_request_sent_yet_in_current_stream_{};
  bool must_send_discovery_request_{};
  bool is_legacy_wildcard_{};

  // Tracks changes in our subscription interest since the previous DeltaDiscoveryRequest we sent.
  // TODO: Can't use absl::flat_hash_set due to ordering issues in gTest expectation matching.
  // Feel free to change to an unordered container once we figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;
};

} // namespace Config
} // namespace Envoy
