#include "common/config/http_subscription_impl.h"

#include <memory>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/config/utility.h"
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
    SubscriptionCallbacks& callbacks, SubscriptionStats stats,
    std::chrono::milliseconds init_fetch_timeout,
    ProtobufMessage::ValidationVisitor& validation_visitor)
    : Http::RestApiFetcher(cm, remote_cluster_name, dispatcher, random, refresh_interval,
                           request_timeout),
      callbacks_(callbacks), stats_(stats), dispatcher_(dispatcher),
      init_fetch_timeout_(init_fetch_timeout), validation_visitor_(validation_visitor) {
  request_.mutable_node()->CopyFrom(local_info.node());
  ASSERT(service_method.options().HasExtension(google::api::http));
  const auto& http_rule = service_method.options().GetExtension(google::api::http);
  path_ = http_rule.post();
  ASSERT(http_rule.body() == "*");
}

// Config::Subscription
void HttpSubscriptionImpl::start(const std::set<std::string>& resource_names) {
  if (init_fetch_timeout_.count() > 0) {
    init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
      ENVOY_LOG(warn, "REST config: initial fetch timed out for", path_);
      callbacks_.onConfigUpdateFailed(nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
  }

  Protobuf::RepeatedPtrField<std::string> resources_vector(resource_names.begin(),
                                                           resource_names.end());
  request_.mutable_resource_names()->Swap(&resources_vector);
  initialize();
}

void HttpSubscriptionImpl::updateResources(const std::set<std::string>& update_to_these_names) {
  Protobuf::RepeatedPtrField<std::string> resources_vector(update_to_these_names.begin(),
                                                           update_to_these_names.end());
  request_.mutable_resource_names()->Swap(&resources_vector);
}

// Http::RestApiFetcher
void HttpSubscriptionImpl::createRequest(Http::Message& request) {
  ENVOY_LOG(debug, "Sending REST request for {}", path_);
  stats_.update_attempt_.inc();
  request.headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Post);
  request.headers().insertPath().value(path_);
  request.body() =
      std::make_unique<Buffer::OwnedImpl>(MessageUtil::getJsonStringFromMessage(request_));
  request.headers().insertContentType().value().setReference(
      Http::Headers::get().ContentTypeValues.Json);
  request.headers().insertContentLength().value(request.body()->length());
}

void HttpSubscriptionImpl::parseResponse(const Http::Message& response) {
  disableInitFetchTimeoutTimer();
  envoy::api::v2::DiscoveryResponse message;
  try {
    MessageUtil::loadFromJson(response.bodyAsString(), message, validation_visitor_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "REST config JSON conversion error: {}", e.what());
    handleFailure(nullptr);
    return;
  }
  try {
    callbacks_.onConfigUpdate(message.resources(), message.version_info());
    request_.set_version_info(message.version_info());
    stats_.version_.set(HashUtil::xxHash64(request_.version_info()));
    stats_.update_success_.inc();
  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "REST config update rejected: {}", e.what());
    stats_.update_rejected_.inc();
    callbacks_.onConfigUpdateFailed(&e);
  }
}

void HttpSubscriptionImpl::onFetchComplete() {}

void HttpSubscriptionImpl::onFetchFailure(const EnvoyException* e) {
  disableInitFetchTimeoutTimer();
  ENVOY_LOG(warn, "REST config update failed: {}", e != nullptr ? e->what() : "fetch failure");
  handleFailure(e);
}

void HttpSubscriptionImpl::handleFailure(const EnvoyException* e) {
  stats_.update_failure_.inc();
  callbacks_.onConfigUpdateFailed(e);
}

void HttpSubscriptionImpl::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

} // namespace Config
} // namespace Envoy
