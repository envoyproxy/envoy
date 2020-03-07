#include "common/config/http_subscription_impl.h"

#include <memory>

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "google/api/annotations.pb.h"

namespace Envoy {
namespace Config {

HttpSubscriptionImpl::HttpSubscriptionImpl(
    const LocalInfo::LocalInfo& local_info, Upstream::ClusterManager& cm,
    const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
    Runtime::RandomGenerator& random, std::chrono::milliseconds refresh_interval,
    std::chrono::milliseconds request_timeout, const Protobuf::MethodDescriptor& service_method,
    absl::string_view type_url, envoy::config::core::v3::ApiVersion transport_api_version,
    SubscriptionCallbacks& callbacks, SubscriptionStats stats,
    std::chrono::milliseconds init_fetch_timeout,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : Http::RestApiFetcher(cm, remote_cluster_name, dispatcher, random, refresh_interval,
                           request_timeout),
      callbacks_(callbacks), stats_(stats), dispatcher_(dispatcher),
      init_fetch_timeout_(init_fetch_timeout), validation_visitor_(validation_visitor),
      transport_api_version_(transport_api_version) {
  request_.mutable_node()->CopyFrom(local_info.node());
  request_.set_type_url(std::string(type_url));
  ASSERT(service_method.options().HasExtension(google::api::http));
  const auto& http_rule = service_method.options().GetExtension(google::api::http);
  path_ = http_rule.post();
  ASSERT(http_rule.body() == "*");
}

// Config::Subscription
void HttpSubscriptionImpl::start(const std::set<std::string>& resource_names) {
  if (init_fetch_timeout_.count() > 0) {
    init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
      handleFailure(Config::ConfigUpdateFailureReason::FetchTimedout, nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
  }

  Protobuf::RepeatedPtrField<std::string> resources_vector(resource_names.begin(),
                                                           resource_names.end());
  request_.mutable_resource_names()->Swap(&resources_vector);
  initialize();
}

void HttpSubscriptionImpl::updateResourceInterest(
    const std::set<std::string>& update_to_these_names) {
  Protobuf::RepeatedPtrField<std::string> resources_vector(update_to_these_names.begin(),
                                                           update_to_these_names.end());
  request_.mutable_resource_names()->Swap(&resources_vector);
}

// Http::RestApiFetcher
void HttpSubscriptionImpl::createRequest(Http::RequestMessage& request) {
  ENVOY_LOG(debug, "Sending REST request for {}", path_);
  stats_.update_attempt_.inc();
  request.headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  request.headers().setPath(path_);
  request.body() = std::make_unique<Buffer::OwnedImpl>(
      VersionConverter::getJsonStringFromMessage(request_, transport_api_version_));
  request.headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  request.headers().setContentLength(request.body()->length());
}

void HttpSubscriptionImpl::parseResponse(const Http::ResponseMessage& response) {
  disableInitFetchTimeoutTimer();
  envoy::service::discovery::v3::DiscoveryResponse message;
  try {
    MessageUtil::loadFromJson(response.bodyAsString(), message, validation_visitor_);
  } catch (const EnvoyException& e) {
    handleFailure(Config::ConfigUpdateFailureReason::UpdateRejected, &e);
    return;
  }
  try {
    callbacks_.onConfigUpdate(message.resources(), message.version_info());
    request_.set_version_info(message.version_info());
    stats_.version_.set(HashUtil::xxHash64(request_.version_info()));
    stats_.update_success_.inc();
  } catch (const EnvoyException& e) {
    handleFailure(Config::ConfigUpdateFailureReason::UpdateRejected, &e);
  }
}

void HttpSubscriptionImpl::onFetchComplete() {}

void HttpSubscriptionImpl::onFetchFailure(Config::ConfigUpdateFailureReason reason,
                                          const EnvoyException* e) {
  handleFailure(reason, e);
}

void HttpSubscriptionImpl::handleFailure(Config::ConfigUpdateFailureReason reason,
                                         const EnvoyException* e) {

  switch (reason) {
  case Config::ConfigUpdateFailureReason::ConnectionFailure:
    ENVOY_LOG(warn, "REST update for {} failed", path_);
    stats_.update_failure_.inc();
    break;
  case Config::ConfigUpdateFailureReason::FetchTimedout:
    ENVOY_LOG(warn, "REST config: initial fetch timeout for {}", path_);
    stats_.init_fetch_timeout_.inc();
    disableInitFetchTimeoutTimer();
    break;
  case Config::ConfigUpdateFailureReason::UpdateRejected:
    ASSERT(e != nullptr);
    ENVOY_LOG(warn, "REST config for {} rejected: {}", path_, e->what());
    stats_.update_rejected_.inc();
    disableInitFetchTimeoutTimer();
    break;
  }

  if (reason == Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure) {
    // New requests will be sent again.
    // If init_fetch_timeout is non-zero, server will continue startup after it timeout
    return;
  }

  callbacks_.onConfigUpdateFailed(reason, e);
}

void HttpSubscriptionImpl::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

} // namespace Config
} // namespace Envoy
