#pragma once

#include <queue>
#include <unordered_map>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/common/time.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/config/grpc_stream.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

/**
 * ADS API implementation that fetches via gRPC.
 */
class GrpcMuxImpl : public GrpcMux,
                    public GrpcStreamCallbacks<envoy::api::v2::DiscoveryResponse>,
                    public Logger::Loggable<Logger::Id::config> {
public:
  GrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
              Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
              Runtime::RandomGenerator& random, Stats::Scope& scope,
              const RateLimitSettings& rate_limit_settings, Server::ConfigTracker& config_tracker,
              const envoy::api::v2::core::GrpcService& grpc_service);
  ~GrpcMuxImpl();

  void start() override;
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks) override;
  void pause(const std::string& type_url) override;
  void resume(const std::string& type_url) override;

  void sendDiscoveryRequest(const std::string& type_url);

  ProtobufTypes::MessagePtr dumpControlPlaneConfig() const;

  // Config::GrpcStreamCallbacks
  void onStreamEstablished() override;
  void onEstablishmentFailure() override;
  void onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) override;
  void onWriteable() override;

  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>&
  grpcStreamForTest() {
    return grpc_stream_;
  }

private:
  void setRetryTimer();
  void populateControlPlaneInfo(const envoy::api::v2::DiscoveryResponse& message);

  struct GrpcMuxWatchImpl : public GrpcMuxWatch {
    GrpcMuxWatchImpl(const std::vector<std::string>& resources, GrpcMuxCallbacks& callbacks,
                     const std::string& type_url, GrpcMuxImpl& parent)
        : resources_(resources), callbacks_(callbacks), type_url_(type_url), parent_(parent),
          inserted_(true) {
      entry_ = parent.api_state_[type_url].watches_.emplace(
          parent.api_state_[type_url].watches_.begin(), this);
    }
    ~GrpcMuxWatchImpl() override {
      if (inserted_) {
        parent_.api_state_[type_url_].watches_.erase(entry_);
        if (!resources_.empty()) {
          parent_.sendDiscoveryRequest(type_url_);
        }
      }
    }
    std::vector<std::string> resources_;
    GrpcMuxCallbacks& callbacks_;
    const std::string type_url_;
    GrpcMuxImpl& parent_;
    std::list<GrpcMuxWatchImpl*>::iterator entry_;
    bool inserted_;
  };

  // Per muxed API state.
  struct ApiState {
    // Watches on the returned resources for the API;
    std::list<GrpcMuxWatchImpl*> watches_;
    // Current DiscoveryRequest for API.
    envoy::api::v2::DiscoveryRequest request_;
    // Paused via pause()?
    bool paused_{};
    // Was a DiscoveryRequest elided during a pause?
    bool pending_{};
    // Has this API been tracked in subscriptions_?
    bool subscribed_{};
  };

  // Request queue management logic.
  void queueDiscoveryRequest(const std::string& queue_item);
  void clearRequestQueue();
  void drainRequests();

  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse> grpc_stream_;
  const LocalInfo::LocalInfo& local_info_;
  std::unordered_map<std::string, ApiState> api_state_;
  // Envoy's dependency ordering.
  std::list<std::string> subscriptions_;
  const std::string& xds_service_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  TimeSource& time_source_;
  envoy::api::v2::core::GrpcService grpc_service_;
  // Per Service Control Plane configuration that Envoy is connected to. For services like
  // RouteDiscoveryService and EndpointDiscovery the configuration allows to specify multiple config
  // sources, for example a different RouteDiscoveryService can be specified for each listener.
  static std::unordered_map<
      std::string,
      std::list<envoy::admin::v2alpha::ControlPlaneConfigDump::ConfigSourceControlPlaneInfo>>
      per_service_control_plane_info_;

  // A queue to store requests while rate limited. Note that when requests cannot be sent due to the
  // gRPC stream being down, this queue does not store them; rather, they are simply dropped.
  // This string is a type URL.
  std::queue<std::string> request_queue_;
};

class NullGrpcMuxImpl : public GrpcMux {
public:
  void start() override {}
  GrpcMuxWatchPtr subscribe(const std::string&, const std::vector<std::string>&,
                            GrpcMuxCallbacks&) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
  void pause(const std::string&) override {}
  void resume(const std::string&) override {}
};

} // namespace Config
} // namespace Envoy
