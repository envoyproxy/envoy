#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/config/api_version.h"
#include "common/http/rest_api_fetcher.h"

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
                       envoy::config::core::v3::ApiVersion transport_api_version,
                       SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
                       SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout,
                       ProtobufMessage::ValidationVisitor& validation_visitor);

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResourceInterest(const std::set<std::string>& update_to_these_names) override;

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
  Config::OpaqueResourceDecoder& resource_decoder_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

} // namespace Config
} // namespace Envoy
