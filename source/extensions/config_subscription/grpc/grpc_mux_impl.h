#pragma once

#include <cstdint>
#include <memory>
#include <queue>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/config/custom_config_validators.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_config_tracker.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/ttl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_context_params.h"
#include "source/common/config/xds_resource.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_context.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_failover.h"

#include "absl/container/node_hash_map.h"
#include "xds/core/v3/resource_name.pb.h"

namespace Envoy {
namespace Config {
/**
 * ADS API implementation that fetches via gRPC.
 */
class GrpcMuxImpl : public GrpcMux,
                    public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse>,
                    public Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(GrpcMuxContext& grpc_mux_context, bool skip_subsequent_node);

  ~GrpcMuxImpl() override;

  // Causes all GrpcMuxImpl objects to stop sending any messages on `grpc_stream_` to fix a crash
  // on Envoy shutdown due to dangling pointers. This may not be the ideal fix; it is probably
  // preferable for the `ServerImpl` to cause all configuration subscriptions to be shutdown, which
  // would then cause all `GrpcMuxImpl` to be destructed.
  // TODO: figure out the correct fix: https://github.com/envoyproxy/envoy/issues/15072.
  static void shutdownAll();

  void shutdown() { shutdown_ = true; }

  void start() override;

  // GrpcMux
  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;

  GrpcMuxWatchPtr addWatch(const std::string& type_url,
                           const absl::flat_hash_set<std::string>& resources,
                           SubscriptionCallbacks& callbacks,
                           OpaqueResourceDecoderSharedPtr resource_decoder,
                           const SubscriptionOptions& options) override;

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
  }

  EdsResourcesCacheOptRef edsResourcesCache() override {
    return makeOptRefFromPtr(eds_resources_cache_.get());
  }

  absl::Status
  updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                  Grpc::RawAsyncClientPtr&& failover_async_client,
                  CustomConfigValidatorsPtr&& custom_config_validators, Stats::Scope& scope,
                  BackOffStrategyPtr&& backoff_strategy,
                  const envoy::config::core::v3::ApiConfigSource& ads_config_source) override;

  void handleDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message);

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure(bool) override;
  void
  onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
                      ControlPlaneStats& control_plane_stats) override;
  void onWriteable() override;

  GrpcStreamInterface<envoy::service::discovery::v3::DiscoveryRequest,
                      envoy::service::discovery::v3::DiscoveryResponse>&
  grpcStreamForTest() {
    // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
    // return grpc_stream_.currentStreamForTest() directly (defined in GrpcMuxFailover).
    if (Runtime::runtimeFeatureEnabled("envoy.restart_features.xds_failover_support")) {
      return dynamic_cast<GrpcMuxFailover<envoy::service::discovery::v3::DiscoveryRequest,
                                          envoy::service::discovery::v3::DiscoveryResponse>*>(
                 grpc_stream_.get())
          ->currentStreamForTest();
    }
    return *grpc_stream_.get();
  }

private:
  // Helper function to create the grpc_stream_ object.
  std::unique_ptr<GrpcStreamInterface<envoy::service::discovery::v3::DiscoveryRequest,
                                      envoy::service::discovery::v3::DiscoveryResponse>>
  createGrpcStreamObject(Grpc::RawAsyncClientPtr&& async_client,
                         Grpc::RawAsyncClientPtr&& failover_async_client,
                         const Protobuf::MethodDescriptor& service_method, Stats::Scope& scope,
                         BackOffStrategyPtr&& backoff_strategy,
                         const RateLimitSettings& rate_limit_settings);

  void drainRequests();
  void setRetryTimer();
  void sendDiscoveryRequest(absl::string_view type_url);
  // Clears the nonces of all subscribed types in this gRPC mux.
  void clearNonce();

  struct GrpcMuxWatchImpl : public GrpcMuxWatch {
    GrpcMuxWatchImpl(const absl::flat_hash_set<std::string>& resources,
                     SubscriptionCallbacks& callbacks,
                     OpaqueResourceDecoderSharedPtr resource_decoder, const std::string& type_url,
                     GrpcMuxImpl& parent, const SubscriptionOptions& options,
                     const LocalInfo::LocalInfo& local_info,
                     EdsResourcesCacheOptRef eds_resources_cache)
        : callbacks_(callbacks), resource_decoder_(resource_decoder), type_url_(type_url),
          parent_(parent), subscription_options_(options), local_info_(local_info),
          watches_(parent.apiStateFor(type_url).watches_),
          eds_resources_cache_(eds_resources_cache) {
      updateResources(resources);
      // If eds resources cache is provided, then the type must be ClusterLoadAssignment.
      ASSERT(
          !eds_resources_cache_.has_value() ||
          (type_url == Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>()));
    }

    ~GrpcMuxWatchImpl() override {
      watches_.erase(iter_);
      if (!resources_.empty()) {
        parent_.queueDiscoveryRequest(type_url_);
        if (eds_resources_cache_.has_value()) {
          removeResourcesFromCache(resources_);
        }
      }
    }

    void update(const absl::flat_hash_set<std::string>& resources) override {
      watches_.erase(iter_);
      if (!resources_.empty()) {
        parent_.queueDiscoveryRequest(type_url_);
      }
      updateResources(resources);
      parent_.queueDiscoveryRequest(type_url_);
    }

    // Maintain deterministic wire ordering via ordered std::set.
    std::set<std::string> resources_;
    SubscriptionCallbacks& callbacks_;
    OpaqueResourceDecoderSharedPtr resource_decoder_;
    const std::string type_url_;
    GrpcMuxImpl& parent_;

  private:
    void updateResources(const absl::flat_hash_set<std::string>& resources) {
      // Finding the list of removed resources by keeping the current resources
      // set until the end the function and computing the diff.
      // Temporarily keep the resources prior to the update to find which ones
      // were removed.
      std::set<std::string> previous_resources;
      previous_resources.swap(resources_);
      std::transform(
          resources.begin(), resources.end(), std::inserter(resources_, resources_.begin()),
          [this](const std::string& resource_name) -> std::string {
            if (XdsResourceIdentifier::hasXdsTpScheme(resource_name)) {
              auto xdstp_resource_or_error = XdsResourceIdentifier::decodeUrn(resource_name);
              THROW_IF_NOT_OK_REF(xdstp_resource_or_error.status());
              auto xdstp_resource = xdstp_resource_or_error.value();
              if (subscription_options_.add_xdstp_node_context_params_) {
                const auto context = XdsContextParams::encodeResource(
                    local_info_.contextProvider().nodeContext(), xdstp_resource.context(), {}, {});
                xdstp_resource.mutable_context()->CopyFrom(context);
              }
              XdsResourceIdentifier::EncodeOptions encode_options;
              encode_options.sort_context_params_ = true;
              return XdsResourceIdentifier::encodeUrn(xdstp_resource, encode_options);
            }
            return resource_name;
          });
      if (eds_resources_cache_.has_value()) {
        // Compute the removed resources and remove them from the cache.
        std::set<std::string> removed_resources;
        std::set_difference(previous_resources.begin(), previous_resources.end(),
                            resources_.begin(), resources_.end(),
                            std::inserter(removed_resources, removed_resources.begin()));
        removeResourcesFromCache(removed_resources);
      }
      // move this watch to the beginning of the list
      iter_ = watches_.emplace(watches_.begin(), this);
    }

    void removeResourcesFromCache(const std::set<std::string>& resources_to_remove) {
      ASSERT(eds_resources_cache_.has_value());
      // Iterate over the resources to remove, and if no other watcher
      // registered for that resource, remove it from the cache.
      for (const auto& resource_name : resources_to_remove) {
        // Counts the number of watchers that watch the resource.
        uint32_t resource_watchers_count = 0;
        for (const auto& watch : watches_) {
          // Skip the current watcher as it is intending to remove the resource.
          if (watch == this) {
            continue;
          }
          if (watch->resources_.find(resource_name) != watch->resources_.end()) {
            resource_watchers_count++;
          }
        }
        // Other than "this" watcher, the resource is not watched by any other
        // watcher, so it can be removed.
        if (resource_watchers_count == 0) {
          eds_resources_cache_->removeResource(resource_name);
        }
      }
    }

    using WatchList = std::list<GrpcMuxWatchImpl*>;
    const SubscriptionOptions& subscription_options_;
    const LocalInfo::LocalInfo& local_info_;
    WatchList& watches_;
    WatchList::iterator iter_;
    // Optional cache for the specific ClusterLoadAssignments of this watch.
    EdsResourcesCacheOptRef eds_resources_cache_;
  };

  // Per muxed API state.
  struct ApiState {
    ApiState(Event::Dispatcher& dispatcher,
             std::function<void(const std::vector<std::string>&)> callback)
        : ttl_(callback, dispatcher, dispatcher.timeSource()) {}

    bool paused() const { return pauses_ > 0; }

    // Watches on the returned resources for the API;
    std::list<GrpcMuxWatchImpl*> watches_;
    // Current DiscoveryRequest for API.
    envoy::service::discovery::v3::DiscoveryRequest request_;
    // Count of unresumed pause() invocations.
    uint32_t pauses_{};
    // Was a DiscoveryRequest elided during a pause?
    bool pending_{};
    // Has this API been tracked in subscriptions_?
    bool subscribed_{};
    // This resource type must have a Node sent at next request.
    bool must_send_node_{};
    TtlManager ttl_;
    // The identifier for the server that sent the most recent response, or
    // empty if there is none.
    std::string control_plane_identifier_{};
    // If true, xDS resources were previously fetched from an xDS source or an xDS delegate.
    bool previously_fetched_data_{false};
  };

  bool isHeartbeatResource(const std::string& type_url, const DecodedResource& resource) {
    return !resource.hasResource() &&
           resource.version() == apiStateFor(type_url).request_.version_info();
  }
  void expiryCallback(absl::string_view type_url, const std::vector<std::string>& expired);
  // Request queue management logic.
  void queueDiscoveryRequest(absl::string_view queue_item);
  // Invoked when dynamic context parameters change for a resource type.
  void onDynamicContextUpdate(absl::string_view resource_type_url);
  // Must be invoked from the main or test thread.
  void loadConfigFromDelegate(const std::string& type_url,
                              const absl::flat_hash_set<std::string>& resource_names);
  // Must be invoked from the main or test thread.
  void processDiscoveryResources(const std::vector<DecodedResourcePtr>& resources,
                                 ApiState& api_state, const std::string& type_url,
                                 const std::string& version_info, bool call_delegate);

  Event::Dispatcher& dispatcher_;
  // Multiplexes the stream to the primary and failover sources.
  // TODO(adisuissa): Once envoy.restart_features.xds_failover_support is deprecated,
  // convert from unique_ptr<GrpcStreamInterface> to GrpcMuxFailover directly.
  std::unique_ptr<GrpcStreamInterface<envoy::service::discovery::v3::DiscoveryRequest,
                                      envoy::service::discovery::v3::DiscoveryResponse>>
      grpc_stream_;
  const LocalInfo::LocalInfo& local_info_;
  const bool skip_subsequent_node_;
  CustomConfigValidatorsPtr config_validators_;
  XdsConfigTrackerOptRef xds_config_tracker_;
  XdsResourcesDelegateOptRef xds_resources_delegate_;
  EdsResourcesCachePtr eds_resources_cache_;
  const std::string target_xds_authority_;
  bool first_stream_request_{true};

  // Helper function for looking up and potentially allocating a new ApiState.
  ApiState& apiStateFor(absl::string_view type_url);

  absl::node_hash_map<std::string, std::unique_ptr<ApiState>> api_state_;

  // Envoy's dependency ordering.
  std::list<std::string> subscriptions_;

  // A queue to store requests while rate limited. Note that when requests
  // cannot be sent due to the gRPC stream being down, this queue does not
  // store them; rather, they are simply dropped. This string is a type
  // URL.
  std::unique_ptr<std::queue<std::string>> request_queue_;

  Common::CallbackHandlePtr dynamic_update_callback_handle_;

  bool started_{false};
  // True iff Envoy is shutting down; no messages should be sent on the `grpc_stream_` when this is
  // true because it may contain dangling pointers.
  std::atomic<bool> shutdown_{false};
};

using GrpcMuxImplPtr = std::unique_ptr<GrpcMuxImpl>;
using GrpcMuxImplSharedPtr = std::shared_ptr<GrpcMuxImpl>;

class GrpcMuxFactory;
DECLARE_FACTORY(GrpcMuxFactory);

} // namespace Config
} // namespace Envoy
