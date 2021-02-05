#pragma once

#include <cstdint>
#include <memory>
#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/cleanup.h"
#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/config/api_version.h"
#include "common/config/grpc_stream.h"
#include "common/config/ttl.h"
#include "common/config/utility.h"
#include "common/config/watch_map.h"
#include "common/runtime/runtime_features.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {
/**
 * ADS API implementation that fetches via gRPC.
 */
class LegacyGrpcMuxImpl
    : public GrpcMux,
      public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse>,
      public Logger::Loggable<Logger::Id::config> {
public:
  LegacyGrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr async_client,
                    Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
                    envoy::config::core::v3::ApiVersion transport_api_version,
                    Random::RandomGenerator& random, Stats::Scope& scope,
                    const RateLimitSettings& rate_limit_settings, bool skip_subsequent_node);
  ~LegacyGrpcMuxImpl() override = default;

  void start() override;

  // GrpcMux
  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;
  bool paused(const std::string&) const override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  Watch* addWatch(const std::string& type_url, const absl::flat_hash_set<std::string>& resources,
                  SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
                  std::chrono::milliseconds init_fetch_timeout,
                  const bool use_namespace_matching = false) override;

  void updateWatch(const std::string&, Watch*, const absl::flat_hash_set<std::string>&, const bool) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void removeWatch(const std::string& type_url, Watch* watch) override;

  void disableInitFetchTimeoutTimer() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void handleDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message);

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void registerVersionedTypeUrl(const std::string& type_url);
  void
  onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message,
                      ControlPlaneStats& control_plane_stats) override;
  void onWriteable() override;

  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>&
  grpcStreamForTest() {
    return grpc_stream_;
  }

  bool isLegacy() const override { return true; }

private:
  void drainRequests();
  void setRetryTimer();
  void sendDiscoveryRequest(const std::string& type_url);

  // Per muxed API state.
  struct ApiState {
    ApiState(Event::Dispatcher& dispatcher,
             std::function<void(const std::vector<std::string>&)> callback)
        : ttl_(callback, dispatcher, dispatcher.timeSource()) {}

    bool paused() const { return pauses_ > 0; }

    // Watches on the returned resources for the API;
    std::list<std::unique_ptr<Watch>> watches_;
    // Current DiscoveryRequest for API.
    envoy::service::discovery::v3::DiscoveryRequest request_;
    // Count of unresumed pause() invocations.
    uint32_t pauses_{};
    // Was a DiscoveryRequest elided during a pause?
    bool pending_{};
    // Has this API been tracked in subscriptions_?
    bool subscribed_{};
    TtlManager ttl_;
  };

  bool isHeartbeatResource(const std::string& type_url, const DecodedResource& resource) {
    return !resource.hasResource() &&
           resource.version() == apiStateFor(type_url).request_.version_info();
  }
  void expiryCallback(const std::string& type_url, const std::vector<std::string>& expired);
  // Request queue management logic.
  void queueDiscoveryRequest(const std::string& queue_item);

  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>
      grpc_stream_;
  const LocalInfo::LocalInfo& local_info_;
  const bool skip_subsequent_node_;
  bool first_stream_request_;

  // Helper function for looking up and potentially allocating a new ApiState.
  ApiState& apiStateFor(const std::string& type_url);

  absl::node_hash_map<std::string, std::unique_ptr<ApiState>> api_state_;

  // Envoy's dependency ordering.
  std::list<std::string> subscriptions_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  // This string is a type URL.
  std::unique_ptr<std::queue<std::string>> request_queue_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;

  Event::Dispatcher& dispatcher_;
  bool enable_type_url_downgrade_and_upgrade_;
};

} // namespace Config
} // namespace Envoy
