#pragma once

#include "envoy/config/ads.h"
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
class AdsApiImpl : public AdsApi,
                   Grpc::AsyncStreamCallbacks<envoy::api::v2::DiscoveryResponse>,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  AdsApiImpl(const envoy::api::v2::Node& node, const envoy::api::v2::ApiConfigSource& ads_config,
             Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher);
  ~AdsApiImpl();

  void start() override;
  AdsWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                        AdsCallbacks& callbacks) override;

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
  void sendDiscoveryRequest(const envoy::api::v2::DiscoveryRequest& request);
  void handleFailure();

  struct AdsWatchImpl : public AdsWatch {
    AdsWatchImpl(const std::vector<std::string>& resources, AdsCallbacks& callbacks,
                 std::list<AdsWatchImpl*>& type_url_list)
        : resources_(resources), callbacks_(callbacks), type_url_list_(type_url_list),
          inserted_(true) {
      entry_ = type_url_list_.emplace(type_url_list_.begin(), this);
    }
    ~AdsWatchImpl() override {
      if (inserted_) {
        type_url_list_.erase(entry_);
      }
    }
    std::vector<std::string> resources_;
    AdsCallbacks& callbacks_;
    std::list<AdsWatchImpl*>& type_url_list_;
    std::list<AdsWatchImpl*>::iterator entry_;
    bool inserted_;
  };

  envoy::api::v2::Node node_;
  std::unique_ptr<
      Grpc::AsyncClient<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse>>
      async_client_;
  Grpc::AsyncStream<envoy::api::v2::DiscoveryRequest>* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  std::unordered_map<std::string, std::list<AdsWatchImpl*>> watches_;
  std::unordered_map<std::string, envoy::api::v2::DiscoveryRequest> requests_;
  // Envoy's dependendency ordering.
  std::list<std::string> subscriptions_;
  Event::TimerPtr retry_timer_;
};

} // namespace Config
} // namespace Envoy
