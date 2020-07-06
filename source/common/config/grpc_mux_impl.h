#pragma once

#include <queue>
#include <unordered_map>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/time.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/cleanup.h"
#include "common/common/logger.h"
#include "common/config/api_version.h"
#include "common/config/grpc_stream.h"
#include "common/config/utility.h"

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
              Runtime::RandomGenerator& random, Stats::Scope& scope,
              const RateLimitSettings& rate_limit_settings, bool skip_subsequent_node);
  ~GrpcMuxImpl() override = default;

  void start() override;

  // GrpcMux
  void pause(const std::string& type_url) override;
  void pause(const std::vector<std::string> type_urls) override;
  void resume(const std::string& type_url) override;
  void resume(const std::vector<std::string> type_urls) override;
  bool paused(const std::string& type_url) const override;
  bool paused(const std::vector<std::string> type_urls) const override;

  GrpcMuxWatchPtr addWatch(const std::string& type_url, const std::set<std::string>& resources,
                           SubscriptionCallbacks& callbacks,
                           OpaqueResourceDecoder& resource_decoder) override;

  void handleDiscoveryResponse(
      std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&& message);

  void sendDiscoveryRequest(const std::string& type_url);

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
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
        parent_.sendDiscoveryRequest(type_url_);
      }
    }

    void update(const std::set<std::string>& resources) override {
      watches_.remove(this);
      if (!resources_.empty()) {
        parent_.sendDiscoveryRequest(type_url_);
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
    // Watches on the returned resources for the API;
    std::list<GrpcMuxWatchImpl*> watches_;
    // Current DiscoveryRequest for API.
    envoy::service::discovery::v3::DiscoveryRequest request_;
    // Paused via pause()?
    bool paused_{};
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
  std::unordered_map<std::string, ApiState> api_state_;
  // Envoy's dependency ordering.
  std::list<std::string> subscriptions_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  // This string is a type URL.
  std::queue<std::string> request_queue_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

class NullGrpcMuxImpl : public GrpcMux,
                        GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  void start() override {}
  void pause(const std::string&) override {}
  void pause(const std::vector<std::string>) override {}
  void resume(const std::string&) override {}
  void resume(const std::vector<std::string>) override {}
  bool paused(const std::string&) const override { return false; }
  bool paused(const std::vector<std::string>) const override { return false; }

  GrpcMuxWatchPtr addWatch(const std::string&, const std::set<std::string>&, SubscriptionCallbacks&,
                           OpaqueResourceDecoder&) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }

  void onWriteable() override {}
  void onStreamEstablished() override {}
  void onEstablishmentFailure() override {}
  void onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&&,
                           ControlPlaneStats&) override {}
};

} // namespace Config
} // namespace Envoy
