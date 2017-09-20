#pragma once

#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"

#include "api/discovery.pb.h"

namespace Envoy {
namespace Config {

/**
 * ADS API implementation that fetches via gRPC.
 */
class GrpcMuxImpl : public GrpcMux,
                    Grpc::AsyncStreamCallbacks<envoy::api::v2::DiscoveryResponse>,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  GrpcMuxImpl(const envoy::api::v2::Node& node, Upstream::ClusterManager& cluster_manager,
              const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
              const Protobuf::MethodDescriptor& service_method);
  GrpcMuxImpl(const envoy::api::v2::Node& node,
              std::unique_ptr<Grpc::AsyncClient<envoy::api::v2::DiscoveryRequest,
                                                envoy::api::v2::DiscoveryResponse>>
                  async_client,
              Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method);
  ~GrpcMuxImpl();

  void start() override;
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks) override;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  void setRetryTimer();
  void establishNewStream();
  void sendDiscoveryRequest(const std::string& type_url);
  void handleFailure();

  struct GrpcMuxWatchImpl : public GrpcMuxWatch {
    GrpcMuxWatchImpl(const std::vector<std::string>& resources, GrpcMuxCallbacks& callbacks,
                     const std::string& type_url, GrpcMuxImpl& parent)
        : resources_(resources), callbacks_(callbacks), type_url_(type_url), parent_(parent),
          inserted_(true) {
      entry_ = parent.watches_[type_url].emplace(parent.watches_[type_url].begin(), this);
    }
    ~GrpcMuxWatchImpl() override {
      if (inserted_) {
        parent_.watches_[type_url_].erase(entry_);
        parent_.sendDiscoveryRequest(type_url_);
      }
    }
    std::vector<std::string> resources_;
    GrpcMuxCallbacks& callbacks_;
    const std::string type_url_;
    GrpcMuxImpl& parent_;
    std::list<GrpcMuxWatchImpl*>::iterator entry_;
    bool inserted_;
  };

  envoy::api::v2::Node node_;
  std::unique_ptr<
      Grpc::AsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>
      async_client_;
  Grpc::AsyncStream<envoy::api::v2::DiscoveryRequest>* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  std::unordered_map<std::string, std::list<GrpcMuxWatchImpl*>> watches_;
  std::unordered_map<std::string, envoy::api::v2::DiscoveryRequest> requests_;
  // Envoy's dependendency ordering.
  std::list<std::string> subscriptions_;
  Event::TimerPtr retry_timer_;
};

class NullGrpcMuxImpl : public GrpcMux {
public:
  void start() {}
  GrpcMuxWatchPtr subscribe(const std::string&, const std::vector<std::string>&,
                            GrpcMuxCallbacks&) {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
};

} // namespace Config
} // namespace Envoy
