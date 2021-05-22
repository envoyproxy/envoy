#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/config/api_version.h"
#include "common/config/pausable_ack_queue.h"
#include "common/config/ttl.h"
#include "common/config/watch_map.h"

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
                         const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                         const bool wildcard);

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

  // This enumeration describes the resource type, which is only relevant for wildcard
  // subscriptions. Depending on its type, the resource will or will not be resent on the initial
  // wildcard subscription.
  enum class ResourceType {
    // Explicitly requested resource type means that we have asked about the resource by updating
    // the subscription interest. Such resources are resent on the initial wildcard request.
    ExplicitlyRequested,
    // Received from server resources are resources that the state knows about only from the server
    // response. Such resources are not resent on the initial wildcard request.
    ReceivedFromServer,
  };

  // Determines the effective resource type. Explicitly requested type overrides the received from
  // server type.
  ResourceType effectiveResourceType(ResourceType old_type, ResourceType new_type) {
    return (old_type == ResourceType::ReceivedFromServer) ? new_type : old_type;
  }

  class ResourceState {
  public:
    ResourceState(absl::optional<std::string> version, ResourceType type)
        : version_(std::move(version)), type_(type) {}

    ResourceState(const envoy::service::discovery::v3::Resource& resource, ResourceType type)
        : ResourceState(resource.version(), type) {}

    // Builds a ResourceState in the waitingForServer state.
    ResourceState(ResourceType type) : ResourceState(absl::nullopt, type) {}

    ResourceType type() const { return type_; }

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool waitingForServer() const { return version_ == absl::nullopt; }

    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
    ResourceType type_;
  };

  // Describes the wildcard mode the subscription is in.
  enum class WildcardMode {
    // This mode is being expressed by sending a wildcard subscription request with an empty
    // resource subscription list.
    Implicit,
    // This mode is being expressed by sending a wildcard subscription request that contains "*"
    // special name in the resource subscription list.
    Explicit,
    // This mode is means no wildcard subscription.
    Disabled,
  };

  void addResourceState(const envoy::service::discovery::v3::Resource& resource, ResourceType type);
  bool setResourceWaitingForServer(const std::string& resource_name);
  void addResourceWaitingForServer(const std::string& resource_name, ResourceType type);
  void removeResourceState(const std::string& resource_name);

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  absl::node_hash_map<std::string, ResourceState> resource_state_;

  // Not all xDS resources supports heartbeats due to there being specific information encoded in
  // an empty response, which is indistinguishable from a heartbeat in some cases. For now we just
  // disable heartbeats for these resources (currently only VHDS).
  const bool supports_heartbeats_;
  TtlManager ttl_;

  const std::string type_url_;
  WildcardMode mode_;
  UntypedConfigUpdateCallbacks& watch_map_;
  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;

  bool any_request_sent_yet_in_current_stream_{};
  bool must_send_discovery_request_{};

  // Tracks changes in our subscription interest since the previous DeltaDiscoveryRequest we sent.
  // TODO: Can't use absl::flat_hash_set due to ordering issues in gTest expectation matching.
  // Feel free to change to an unordered container once we figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;
};

} // namespace Config
} // namespace Envoy
