#pragma once

#include "envoy/config/subscription.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/local_info/local_info.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/api_version.h"
#include "source/common/config/ttl.h"
#include "source/extensions/config_subscription/grpc/pausable_ack_queue.h"
#include "source/extensions/config_subscription/grpc/watch_map.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {

// Tracks the xDS protocol state of an individual ongoing delta xDS session, i.e. a single type_url.
// There can be multiple DeltaSubscriptionStates active. They will always all be blissfully
// unaware of each other's existence, even when their messages are being multiplexed together by
// ADS.
//
// There are two scenarios which affect how DeltaSubscriptionState manages the resources. First
// scenario is when we are subscribed to a wildcard resource, and other scenario is when we are not.
//
// Delta subscription state also divides the resources it cached into three categories: requested,
// wildcard and ambiguous.
//
// The "requested" category is for resources that we have explicitly asked for (either through the
// initial set of resources or through the on-demand mechanism). Resources in this category are in
// one of two states: "complete" and "waiting for server".
//
// "Complete" resources are resources about which the server sent us the information we need (for
// now - just resource version).
//
// The "waiting for server" state is either for resources that we have just requested, but we still
// didn't receive any version information from the server, or for the "complete" resources that,
// according to the server, are gone, but we are still interested in them - in such case we strip
// the information from the resource.
//
// The "wildcard" category is for resources that we are not explicitly interested in, but we are
// indirectly interested through the subscription to the wildcard resource.
//
// The "ambiguous" category is for resources that we stopped being interested in, but we may still
// be interested indirectly through the wildcard subscription. This situation happens because of the
// xDS protocol limitation - the server isn't able to tell us that the resource we subscribed to is
// also a part of our wildcard subscription. So when we unsubscribe from the resource, we need to
// receive a confirmation from the server whether to keep the resource (which means that it was a
// part of our wildcard subscription) or to drop it.
//
// Please refer to drawings (non-wildcard-resource-state-machine.png and
// (wildcard-resource-state-machine.png) for visual depictions of the resource state machine.
//
// In the "no wildcard subscription" scenario all the cached resources should be in the "requested"
// category. Resources are added to the category upon the explicit request and dropped when we
// explicitly unsubscribe from it. Transitions between "complete" and "waiting for server" happen
// when we receive messages from the server - if a resource in the message is in "added resources"
// list (thus contains version information), the resource becomes "complete". If the resource in the
// message is in "removed resources" list, it changes into the "waiting for server" state. If a
// server sends us a resource that we didn't request, it's going to be ignored.
//
// In the "wildcard subscription" scenario, "requested" category is the same as in "no wildcard
// subscription" scenario, with one exception - the unsubscribed "complete" resource is not removed
// from the cache, but it's moved to the "ambiguous" resources instead. At this point we are waiting
// for the server to tell us that this resource should be either moved to the "wildcard" resources,
// or dropped. Resources in "wildcard" category are only added there or dropped from there by the
// server. Resources from both "wildcard" and "ambiguous" categories can become "requested"
// "complete" resources if we subscribe to them again.
//
// The delta subscription state transitions between the two scenarios depending on whether we are
// subscribed to wildcard resource or not. Nothing special happens when we transition from "no
// wildcard subscription" to "wildcard subscription" scenario, but when transitioning in the other
// direction, we drop all the resources in "wildcard" and "ambiguous" categories.
class DeltaSubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& watch_map,
                         const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                         XdsConfigTrackerOptRef xds_config_tracker);

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed);
  void setMustSendDiscoveryRequest() { must_send_discovery_request_ = true; }

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const;

  // Marks the stream as fresh for the next reconnection attempt. If
  // should_send_initial_resource_versions is true, then the next request will
  // also populate the initial_resource_versions field in the first request (if
  // there are relevant resources).
  void markStreamFresh(bool should_send_initial_resource_versions);

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
    // Builds a ResourceState in the waitingForServer state.
    ResourceState() = default;
    // Builds a ResourceState with a specific version
    ResourceState(absl::string_view version) : version_(version) {}
    // Self-documenting alias of default constructor.
    static ResourceState waitingForServer() { return {}; }
    // Self-documenting alias of constructor with version.
    static ResourceState withVersion(absl::string_view version) { return {version}; }

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool isWaitingForServer() const { return version_ == absl::nullopt; }

    void setAsWaitingForServer() { version_ = absl::nullopt; }
    void setVersion(absl::string_view version) { version_ = std::string(version); }

    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
  };

  void addResourceStateFromServer(const envoy::service::discovery::v3::Resource& resource);
  OptRef<ResourceState> getRequestedResourceState(absl::string_view resource_name);
  OptRef<const ResourceState> getRequestedResourceState(absl::string_view resource_name) const;

  bool isInitialRequestForLegacyWildcard();

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  absl::node_hash_map<std::string, ResourceState> requested_resource_state_;
  // A map from resource name to per-resource version. The keys of this map are resource names we
  // have received as a part of the wildcard subscription.
  absl::node_hash_map<std::string, std::string> wildcard_resource_state_;
  // Used for storing resources that we lost interest in, but could
  // also be a part of wildcard subscription.
  absl::node_hash_map<std::string, std::string> ambiguous_resource_state_;

  // Not all xDS resources supports heartbeats due to there being specific information encoded in
  // an empty response, which is indistinguishable from a heartbeat in some cases. For now we just
  // disable heartbeats for these resources (currently only VHDS).
  const bool supports_heartbeats_;
  TtlManager ttl_;

  const std::string type_url_;
  UntypedConfigUpdateCallbacks& watch_map_;
  const LocalInfo::LocalInfo& local_info_;
  XdsConfigTrackerOptRef xds_config_tracker_;

  bool in_initial_legacy_wildcard_{true};
  bool any_request_sent_yet_in_current_stream_{};
  bool should_send_initial_resource_versions_{true};
  bool must_send_discovery_request_{};

  // Tracks changes in our subscription interest since the previous DeltaDiscoveryRequest we sent.
  // TODO: Can't use absl::flat_hash_set due to ordering issues in gTest expectation matching.
  // Feel free to change to an unordered container once we figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;
};

} // namespace Config
} // namespace Envoy
