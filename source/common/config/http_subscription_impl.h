#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

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
                       Runtime::RandomGenerator& random, std::chrono::milliseconds refresh_interval,
                       std::chrono::milliseconds request_timeout,
                       const Protobuf::MethodDescriptor& service_method,
                       SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                       std::chrono::milliseconds init_fetch_timeout,
                       ProtobufMessage::ValidationVisitor& validation_visitor);

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResources(const std::set<std::string>& update_to_these_names) override;

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(const EnvoyException* e) override;

private:
  void handleFailure(const EnvoyException* e);
  void disableInitFetchTimeoutTimer();

  std::string path_;
  Protobuf::RepeatedPtrField<std::string> resources_;
  envoy::api::v2::DiscoveryRequest request_;
  Config::SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  Event::Dispatcher& dispatcher_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace Config
} // namespace Envoy
