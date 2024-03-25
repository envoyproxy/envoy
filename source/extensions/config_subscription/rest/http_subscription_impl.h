#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/config/type_to_endpoint.h"
#include "source/common/config/utility.h"
#include "source/extensions/config_subscription/rest/rest_api_fetcher.h"

namespace Envoy {
namespace Config {

/**
 * REST implementation of the API Subscription interface. This fetches the API via periodic polling
 * with jitter (based on RestApiFetcher). The REST requests are POSTs of the JSON canonical
 * representation of the DiscoveryRequest proto and the responses are in the form of the JSON
 * canonical representation of DiscoveryResponse. This implementation is responsible for translating
 * between the proto serializable objects in the Subscription API and the REST JSON representation.
 */
class HttpSubscriptionImpl : public Http::RestApiFetcher,
                             public Config::Subscription,
                             Logger::Loggable<Logger::Id::config> {
public:
  HttpSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Upstream::ClusterManager& cm,
                       const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
                       Random::RandomGenerator& random, std::chrono::milliseconds refresh_interval,
                       std::chrono::milliseconds request_timeout,
                       const Protobuf::MethodDescriptor& service_method, absl::string_view type_url,
                       SubscriptionCallbacks& callbacks,
                       OpaqueResourceDecoderSharedPtr resource_decoder, SubscriptionStats stats,
                       std::chrono::milliseconds init_fetch_timeout,
                       ProtobufMessage::ValidationVisitor& validation_visitor);

  // Config::Subscription
  void start(const absl::flat_hash_set<std::string>& resource_names) override;
  void
  updateResourceInterest(const absl::flat_hash_set<std::string>& update_to_these_names) override;
  void requestOnDemandUpdate(const absl::flat_hash_set<std::string>&) override {
    ENVOY_BUG(false, "unexpected request for on demand update");
  }

  // Http::RestApiFetcher
  void createRequest(Http::RequestMessage& request) override;
  void parseResponse(const Http::ResponseMessage& response) override;
  void onFetchComplete() override;
  void onFetchFailure(Config::ConfigUpdateFailureReason reason, const EnvoyException* e) override;

private:
  void handleFailure(Config::ConfigUpdateFailureReason reason, const EnvoyException* e);
  void disableInitFetchTimeoutTimer();

  std::string path_;
  Protobuf::RepeatedPtrField<std::string> resources_;
  envoy::service::discovery::v3::DiscoveryRequest request_;
  Config::SubscriptionCallbacks& callbacks_;
  Config::OpaqueResourceDecoderSharedPtr resource_decoder_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

class HttpSubscriptionFactory : public ConfigSubscriptionFactory {
public:
  std::string name() const override { return "envoy.config_subscription.rest"; }

  /**
   * Extract refresh_delay as a std::chrono::milliseconds from
   * envoy::config::core::v3::ApiConfigSource.
   */
  static std::chrono::milliseconds
  apiConfigSourceRefreshDelay(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Extract request_timeout as a std::chrono::milliseconds from
   * envoy::config::core::v3::ApiConfigSource. If request_timeout isn't set in the config source, a
   * default value of 1s will be returned.
   */
  static std::chrono::milliseconds
  apiConfigSourceRequestTimeout(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  SubscriptionPtr create(SubscriptionData& data) override {
    const envoy::config::core::v3::ApiConfigSource& api_config_source =
        data.config_.api_config_source();
    return std::make_unique<HttpSubscriptionImpl>(
        data.local_info_, data.cm_, api_config_source.cluster_names()[0], data.dispatcher_,
        data.api_.randomGenerator(), apiConfigSourceRefreshDelay(api_config_source),
        apiConfigSourceRequestTimeout(api_config_source), restMethod(data.type_url_),
        data.type_url_, data.callbacks_, data.resource_decoder_, data.stats_,
        Utility::configSourceInitialFetchTimeout(data.config_), data.validation_visitor_);
  }
};

} // namespace Config
} // namespace Envoy
