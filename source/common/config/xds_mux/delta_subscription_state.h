#pragma once

#include "envoy/grpc/status.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/api_version.h"
#include "source/common/config/xds_mux/subscription_state.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Config {
namespace XdsMux {

// Tracks the state of a delta xDS-over-gRPC protocol session.
class DeltaSubscriptionState
    : public BaseSubscriptionState<envoy::service::discovery::v3::DeltaDiscoveryResponse,
                                   envoy::service::discovery::v3::DeltaDiscoveryRequest> {
public:
  DeltaSubscriptionState(std::string type_url, UntypedConfigUpdateCallbacks& watch_map,
                         Event::Dispatcher& dispatcher);

  ~DeltaSubscriptionState() override;

  // Update which resources we're interested in subscribing to.
  void updateSubscriptionInterest(const absl::flat_hash_set<std::string>& cur_added,
                                  const absl::flat_hash_set<std::string>& cur_removed) override;

  // Whether there was a change in our subscription interest we have yet to inform the server of.
  bool subscriptionUpdatePending() const override;

  void markStreamFresh() override { any_request_sent_yet_in_current_stream_ = false; }

  void ttlExpiryCallback(const std::vector<std::string>& expired) override;

  DeltaSubscriptionState(const DeltaSubscriptionState&) = delete;
  DeltaSubscriptionState& operator=(const DeltaSubscriptionState&) = delete;

private:
  std::unique_ptr<envoy::service::discovery::v3::DeltaDiscoveryRequest>
  getNextRequestInternal() override;

  void setResourceTtl(const envoy::service::discovery::v3::Resource& resource);
  bool isHeartbeatResource(const envoy::service::discovery::v3::Resource& resource) const;
  void
  handleGoodResponse(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message) override;
  void addResourceStateFromServer(const envoy::service::discovery::v3::Resource& resource);

  class ResourceState {
  public:
    // Builds a ResourceVersion in the waitingForServer state.
    ResourceState() = default;
    // Builds a ResourceState with a specific version
    ResourceState(absl::string_view version) : version_(version) {}
    // Self-documenting alias of default constructor.
    static ResourceState waitingForServer() { return ResourceState(); }
    // Self-documenting alias of constructor with version.
    static ResourceState withVersion(absl::string_view version) { return ResourceState(version); }

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

  OptRef<ResourceState> getRequestedResourceState(absl::string_view resource_name);
  OptRef<const ResourceState> getRequestedResourceState(absl::string_view resource_name) const;

  bool isInitialRequestForLegacyWildcard();

  // Not all xDS resources support heartbeats due to there being specific information encoded in
  // an empty response, which is indistinguishable from a heartbeat in some cases. For now we just
  // disable heartbeats for these resources (currently only VHDS).
  const bool supports_heartbeats_;

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

  bool in_initial_legacy_wildcard_{true};
  bool any_request_sent_yet_in_current_stream_{};

  // Tracks changes in our subscription interest since the previous DeltaDiscoveryRequest we sent.
  // TODO: Can't use absl::flat_hash_set due to ordering issues in gTest expectation matching.
  // Feel free to change to an unordered container once we figure out how to make it work.
  std::set<std::string> names_added_;
  std::set<std::string> names_removed_;
};

class DeltaSubscriptionStateFactory : public SubscriptionStateFactory<DeltaSubscriptionState> {
public:
  DeltaSubscriptionStateFactory(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
  ~DeltaSubscriptionStateFactory() override = default;
  std::unique_ptr<DeltaSubscriptionState>
  makeSubscriptionState(const std::string& type_url, UntypedConfigUpdateCallbacks& callbacks,
                        OpaqueResourceDecoderSharedPtr,
                        XdsResourcesDelegateOptRef /*xds_resources_delegate*/,
                        const std::string& /*target_xds_authority*/) override {
    return std::make_unique<DeltaSubscriptionState>(type_url, callbacks, dispatcher_);
  }

private:
  Event::Dispatcher& dispatcher_;
};

} // namespace XdsMux
} // namespace Config
} // namespace Envoy
