#pragma once

#include <queue>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/common/token_bucket.h"
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/common/token_bucket_impl.h"
#include "common/config/grpc_stream.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

struct ResourceNameDiff {
  std::vector<std::string> added_;
  std::vector<std::string> removed_;
};

/**
 * TODO SOMETHING SOMETHING subscription. Also handles per-xDS API stats/logging.
 */
template <class ResourceType>
class IncrementalSubscriptionImpl
    : public IncrementalSubscription<ResourceType>,
      public GrpcStream<envoy::api::v2::IncrementalDiscoveryRequest,
                        envoy::api::v2::IncrementalDiscoveryResponse, ResourceNameDiff> {
public:
  IncrementalSubscriptionImpl(const LocalInfo::LocalInfo& local_info,
                              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                              const Protobuf::MethodDescriptor& service_method,
                              Runtime::RandomGenerator& random, Stats::Scope& scope,
                              const RateLimitSettings& rate_limit_settings,
                              IncrementalSubscriptionStats stats)
      : GrpcStream<envoy::api::v2::IncrementalDiscoveryRequest,
                   envoy::api::v2::IncrementalDiscoveryResponse, ResourceNameDiff>(
            std::move(async_client), service_method, random, dispatcher, scope,
            rate_limit_settings),
        type_url_(Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())),
        local_info_(local_info), stats_(stats) {
    establishNewStream();
  }

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resources_. Updates resources_ to
  // 'resources'.
  void buildAndQueueDiscoveryRequest(const std::vector<std::string>& resources) {
    ResourceNameDiff diff;
    for (const auto& resource : resources) {
      if (resources_.find(resource) == resources_.end()) {
        diff.added_.push_back(resource);
      }
    }
    for (const auto& entry : resources_) {
      bool found = false;
      for (const auto& resource : resources) {
        if (entry.first == resource) {
          found = true;
          break;
        }
      }
      if (!found) {
        diff.removed_.push_back(entry.first);
      }
    }
    for (const auto& added : diff.added_) {
      resources_[added] = "0";
    }
    for (const auto& removed : diff.removed_) {
      resources_.erase(removed);
    }
    queueDiscoveryRequest(diff);
  }

  void sendDiscoveryRequest(const ResourceNameDiff& diff) override {
    if (!grpcStreamAvailable()) {
      // Don't immediately try to reconnect: we rely on retry_timer_ for that.
      ENVOY_LOG(debug, "No stream available to sendDiscoveryRequest for {}", type_url_);
      return;
    }
    if (paused_) {
      ENVOY_LOG(trace, "API {} paused during sendDiscoveryRequest().", type_url_);
      return;
    }

    request_.clear_resource_names_subscribe();
    request_.clear_resource_names_unsubscribe();
    std::copy(diff.added_.begin(), diff.added_.end(),
              request_.mutable_resource_names_subscribe()->begin());
    std::copy(diff.removed_.begin(), diff.removed_.end(),
              request_.mutable_resource_names_unsubscribe()->begin());

    ENVOY_LOG(trace, "Sending DiscoveryRequest for {}: {}", type_url_, request_.DebugString());
    sendMessage(request_);
    request_.clear_error_detail();
    request_.clear_initial_resource_versions();
  }

  void subscribe(const std::vector<std::string>& resources) {
    ENVOY_LOG(debug, "incremental subscribe for " + type_url_);

    // Lazily kick off the requests based on first subscription. This has the
    // convenient side-effect that we order messages on the channel based on
    // Envoy's internal dependency ordering.
    if (!subscribed_) {
      request_.set_type_url(type_url_);
      request_.mutable_node()->MergeFrom(local_info_.node());
      subscribed_ = true;
    }
    buildAndQueueDiscoveryRequest(resources);
  }

  void pause() {
    ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url_);
    ASSERT(!paused_);
    paused_ = true;
  }

  void resume() {
    ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url_);
    ASSERT(paused_);
    paused_ = false;
  }

  // Config::IncrementalSubscription....Callbacks? these are just meant as wrappers. Not sure if
  // this class would have these called on it, but perhaps.
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& version_info) {
    callbacks_->onConfigUpdate(added_resources, removed_resources, version_info);
    // TODO update versions of added_resources and removed_resources into resources_.... well,
    // i guess removed_resources just get removed? since there's no provision for "it got removed"
    // to have a version associated here; it's just a list of string resource names.
    stats_.update_success_.inc();
    stats_.update_attempt_.inc();
    stats_.version_.set(HashUtil::xxHash64(version_info));
    ENVOY_LOG(debug, "Incremental config for {} accepted with {} resources added, {} removed",
              type_url_, added_resources.size(), removed_resources.size());
  }

  void
  handleResponse(std::unique_ptr<envoy::api::v2::IncrementalDiscoveryResponse>&& message) override {
    ENVOY_LOG(debug, "Received gRPC message for {} at version {}", type_url_,
              message->system_version_info());

    request_.set_response_nonce(message->nonce());

    try {
      onConfigUpdate(message->resources(), message->removed_resources(),
                     message->system_version_info());
    } catch (const EnvoyException& e) {
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "incremental config for {} rejected: {}", type_url_, e.what());
      stats_.update_attempt_.inc();
      if (callbacks_) {
        callbacks_->onConfigUpdateFailed(&e);
      }
      ::google::rpc::Status* error_detail = request_.mutable_error_detail();
      error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
      error_detail->set_message(e.what());
    }
    queueDiscoveryRequest(ResourceNameDiff()); // no change to subscribed resources
  }

  void handleStreamEstablished() override {
    // TODO need something like this? to guarantee this initial version'd request_ is what gets
    // sent? request_queue_.clear(); "must be populated for first request in a stream"
    request_.clear_initial_resource_versions();
    for (auto const& resource : resources_) {
      (*request_.mutable_initial_resource_versions())[resource.first] = resource.second;
    }
    queueDiscoveryRequest(ResourceNameDiff()); // no change to subscribed resources
  }

  void handleEstablishmentFailure() override {
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "incremental update for {} failed", type_url_);
    stats_.update_attempt_.inc();
    if (callbacks_) {
      callbacks_->onConfigUpdateFailed(nullptr);
    }
  }

  // Config::IncrementalSubscription
  // TODO can combine with updateResources unless API needs to expose them both
  void start(const std::vector<std::string>& resources,
             IncrementalSubscriptionCallbacks<ResourceType>& callbacks) override {
    callbacks_ = &callbacks;

    subscribe(resources);
    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // individual IncrementalSubscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    subscribe(resources);
    stats_.update_attempt_.inc();
  }

private:
  // A map from resource name to per-resource version.
  std::map<std::string, std::string> resources_;
  const std::string type_url_;
  IncrementalSubscriptionCallbacks<ResourceType>* callbacks_{};
  // In-flight or previously sent request.
  envoy::api::v2::IncrementalDiscoveryRequest request_;
  // Paused via pause()?
  bool paused_{};
  // Has this API been tracked in subscriptions_?
  bool subscribed_{};

  const LocalInfo::LocalInfo& local_info_;

  std::queue<ResourceNameDiff> request_queue_;
  // Detects when Envoy is making too many requests.

  IncrementalSubscriptionStats stats_; // TODO trim?
};

} // namespace Config
} // namespace Envoy
