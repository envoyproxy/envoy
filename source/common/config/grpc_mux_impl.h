#pragma once

#include <queue>
#include <unordered_map>

#include "envoy/common/time.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/status.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/cleanup.h"
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
  GrpcMuxImpl(const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr async_client,
              Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
              Runtime::RandomGenerator& random, Stats::Scope& scope,
              const RateLimitSettings& rate_limit_settings, bool skip_subsequent_node);
  ~GrpcMuxImpl() override;

  void start() override;
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::set<std::string>& resources,
                            GrpcMuxCallbacks& callbacks) override;

  // GrpcMux
  // TODO(fredlas) PR #8478 will remove this.
  bool isDelta() const override { return false; }
  void pause(const std::string& type_url) override;
  void resume(const std::string& type_url) override;
  bool paused(const std::string& type_url) const override;

  Watch* addOrUpdateWatch(const std::string&, Watch*, const std::set<std::string>&,
                          SubscriptionCallbacks&, std::chrono::milliseconds) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void removeWatch(const std::string&, Watch*) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  void handleDiscoveryResponse(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message);

  void sendDiscoveryRequest(const std::string& type_url);

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
  void drainRequests();
  void setRetryTimer();

  struct GrpcMuxWatchImpl : public GrpcMuxWatch, RaiiListElement<GrpcMuxWatchImpl*> {
    GrpcMuxWatchImpl(const std::set<std::string>& resources, GrpcMuxCallbacks& callbacks,
                     const std::string& type_url, GrpcMuxImpl& parent)
        : RaiiListElement<GrpcMuxWatchImpl*>(parent.api_state_[type_url].watches_, this),
          resources_(resources), callbacks_(callbacks), type_url_(type_url), parent_(parent),
          inserted_(true) {}
    ~GrpcMuxWatchImpl() override {
      if (inserted_) {
        erase();
        if (!resources_.empty()) {
          parent_.sendDiscoveryRequest(type_url_);
        }
      }
    }

    void clear() {
      inserted_ = false;
      cancel();
    }

    std::set<std::string> resources_;
    GrpcMuxCallbacks& callbacks_;
    const std::string type_url_;
    GrpcMuxImpl& parent_;

  private:
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

  GrpcStream<envoy::api::v2::DiscoveryRequest, envoy::api::v2::DiscoveryResponse> grpc_stream_;
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
};

class NullGrpcMuxImpl : public GrpcMux, GrpcStreamCallbacks<envoy::api::v2::DiscoveryResponse> {
public:
  void start() override {}
  GrpcMuxWatchPtr subscribe(const std::string&, const std::set<std::string>&,
                            GrpcMuxCallbacks&) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
  // TODO(fredlas) PR #8478 will remove this.
  bool isDelta() const override { return false; }
  void pause(const std::string&) override {}
  void resume(const std::string&) override {}
  bool paused(const std::string&) const override { return false; }

  Watch* addOrUpdateWatch(const std::string&, Watch*, const std::set<std::string>&,
                          SubscriptionCallbacks&, std::chrono::milliseconds) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }
  void removeWatch(const std::string&, Watch*) override {
    throw EnvoyException("ADS must be configured to support an ADS config source");
  }

  void onWriteable() override {}
  void onStreamEstablished() override {}
  void onEstablishmentFailure() override {}
  void onDiscoveryResponse(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&&) override {}
};

} // namespace Config
} // namespace Envoy
