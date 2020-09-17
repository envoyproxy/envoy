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
#include "common/config/utility.h"
#include "common/runtime/runtime_features.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Config {
/**
 * ADS API implementation that fetches via gRPC.
 */
class GrpcMuxImpl : public GrpcMux,
                    public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse>,
                    public Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr async_client,
              Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
              envoy::config::core::v3::ApiVersion transport_api_version,
              Random::RandomGenerator& random, Stats::Scope& scope,
              const RateLimitSettings& rate_limit_settings, bool skip_subsequent_node);
  ~GrpcMuxImpl() override = default;

  void start() override;

  // GrpcMux
  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;

  GrpcMuxWatchPtr addWatch(const std::string& type_url, const std::set<std::string>& resources,
                           SubscriptionCallbacks& callbacks,
                           OpaqueResourceDecoder& resource_decoder,
                           const bool use_namespace_matching = false) override;

  void requestOnDemandUpdate(const std::string&, const std::set<std::string>&) override {
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

private:
  void drainRequests();
  void setRetryTimer();
  void sendDiscoveryRequest(const std::string& type_url);

  struct GrpcMuxWatchImpl : public GrpcMuxWatch {
    GrpcMuxWatchImpl(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks,
                     OpaqueResourceDecoder& resource_decoder, const std::string& type_url,
                     GrpcMuxImpl& parent)
        : resources_(resources), callbacks_(callbacks), resource_decoder_(resource_decoder),
          type_url_(type_url), parent_(parent), watches_(parent.api_state_[type_url].watches_) {
      watches_.emplace(watches_.begin(), this);
    }

    ~GrpcMuxWatchImpl() override {
      watches_.remove(this);
      if (!resources_.empty()) {
        parent_.queueDiscoveryRequest(type_url_);
      }
    }

    void update(const std::set<std::string>& resources) override {
      watches_.remove(this);
      if (!resources_.empty()) {
        parent_.queueDiscoveryRequest(type_url_);
      }
      resources_ = resources;
      // move this watch to the beginning of the list
      watches_.emplace(watches_.begin(), this);
      parent_.queueDiscoveryRequest(type_url_);
    }

    std::set<std::string> resources_;
    SubscriptionCallbacks& callbacks_;
    OpaqueResourceDecoder& resource_decoder_;
    const std::string type_url_;
    GrpcMuxImpl& parent_;

  private:
    std::list<GrpcMuxWatchImpl*>& watches_;
  };

  // Per muxed API state.
  struct ApiState {
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
  };

  // Request queue management logic.
  void queueDiscoveryRequest(const std::string& queue_item);

  GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
             envoy::service::discovery::v3::DiscoveryResponse>
      grpc_stream_;
  const LocalInfo::LocalInfo& local_info_;
  const bool skip_subsequent_node_;
  bool first_stream_request_;
  absl::node_hash_map<std::string, ApiState> api_state_;
  // Envoy's dependency ordering.
  std::list<std::string> subscriptions_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  // This string is a type URL.
  std::unique_ptr<std::queue<std::string>> request_queue_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
  bool enable_type_url_downgrade_and_upgrade_;
};

using GrpcMuxImplPtr = std::unique_ptr<GrpcMuxImpl>;
using GrpcMuxImplSharedPtr = std::shared_ptr<GrpcMuxImpl>;

class NullGrpcMuxImpl : public GrpcMux,
                        GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  void start() override {}
  ScopedResume pause(const std::string&) override {
    return std::make_unique<Cleanup>([] {});
  }
  ScopedResume pause(const std::vector<std::string>) override {
    return std::make_unique<Cleanup>([] {});
  }

  GrpcMuxWatchPtr addWatch(const std::string&, const std::set<std::string>&, SubscriptionCallbacks&,
                           OpaqueResourceDecoder&, const bool) override {
    ExceptionUtil::throwEnvoyException("ADS must be configured to support an ADS config source");
  }

  void requestOnDemandUpdate(const std::string&, const std::set<std::string>&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  void onWriteable() override {}
  void onStreamEstablished() override {}
  void onEstablishmentFailure() override {}
  void onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&&,
                           ControlPlaneStats&) override {}
};

} // namespace Config
} // namespace Envoy
