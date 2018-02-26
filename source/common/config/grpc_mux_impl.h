#pragma once

#include <unordered_map>

#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/status.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Config {

/**
 * ADS API implementation that fetches via gRPC.
 */
class GrpcMuxImpl : public GrpcMux,
                    Grpc::TypedAsyncStreamCallbacks<envoy::api::v2::DiscoveryResponse>,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  GrpcMuxImpl(const envoy::api::v2::core::Node& node, Grpc::AsyncClientPtr async_client,
              Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method);
  ~GrpcMuxImpl();

  void start() override;
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks) override;
  void pause(const std::string& type_url) override;
  void resume(const std::string& type_url) override;

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

  envoy::api::v2::core::Node node_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  std::unordered_map<std::string, ApiState> api_state_;
  // Envoy's dependendency ordering.
  std::list<std::string> subscriptions_;
  Event::TimerPtr retry_timer_;
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
