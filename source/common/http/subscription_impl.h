#pragma once

#include "envoy/config/subscription.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/http/headers.h"
#include "common/http/rest_api_fetcher.h"

#include "api/base.pb.h"
#include "google/api/annotations.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/util/json_util.h"

namespace Envoy {
namespace Http {

template <class ResourceType>
class SubscriptionImpl : public RestApiFetcher, Config::Subscription<ResourceType> {
public:
  SubscriptionImpl(const envoy::api::v2::Node& node, Upstream::ClusterManager& cm,
                   const std::string& remote_cluster_name, Event::Dispatcher& dispatcher,
                   Runtime::RandomGenerator& random, std::chrono::milliseconds refresh_interval,
                   const google::protobuf::MethodDescriptor& service_method)
      : RestApiFetcher(cm, remote_cluster_name, dispatcher, random, refresh_interval) {
    request_.mutable_node()->CopyFrom(node);
    ASSERT(service_method.options().HasExtension(google::api::http));
    const auto& http_rule = service_method.options().GetExtension(google::api::http);
    path_ = http_rule.post();
    ASSERT(http_rule.body() == "*");
  }

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    ASSERT(callbacks_ == nullptr);
    google::protobuf::RepeatedPtrField<std::string> resources_vector(resources.begin(),
                                                                     resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
    callbacks_ = &callbacks;
    initialize();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    google::protobuf::RepeatedPtrField<std::string> resources_vector(resources.begin(),
                                                                     resources.end());
    request_.mutable_resource_names()->Swap(&resources_vector);
  }

  // Http::RestApiFetcher
  void createRequest(Message& request) override {
    google::protobuf::util::JsonOptions json_options;
    std::string request_json;
    const auto status =
        google::protobuf::util::MessageToJsonString(request_, &request_json, json_options);
    // If the status isn't OK, we just send an empty body.
    ASSERT(status == google::protobuf::util::Status::OK);
    request.headers().insertMethod().value(Http::Headers::get().MethodValues.Post);
    request.headers().insertPath().value(path_);
    request.body().reset(new Buffer::OwnedImpl(request_json));
  }

  void parseResponse(const Message& response) override {
    envoy::api::v2::DiscoveryResponse message;
    const auto status =
        google::protobuf::util::JsonStringToMessage(response.bodyAsString(), &message);
    if (status != google::protobuf::util::Status::OK) {
      // TODO(htuch): Track stats and log failures.
      return;
    }
    const auto typed_resources = Config::Utility::getTypedResources<ResourceType>(message);
    if (callbacks_->onConfigUpdate(typed_resources)) {
      request_.set_version_info(message.version_info());
    }
  }

  void onFetchComplete() override {}

  void onFetchFailure(EnvoyException* e) override {
    // TODO(htuch): Track stats and log failures.
    UNREFERENCED_PARAMETER(e);
  }

private:
  std::string path_;
  google::protobuf::RepeatedPtrField<std::string> resources_;
  Config::SubscriptionCallbacks<ResourceType>* callbacks_{};
  envoy::api::v2::DiscoveryRequest request_;
};

} // namespace Http
} // namespace Envoy
