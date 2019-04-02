#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/grpc/status.h"

namespace Envoy {
namespace Config {

struct ResourceNameDiff {
  std::vector<std::string> added_;
  std::vector<std::string> removed_;
  std::string type_url_;
  ResourceNameDiff(std::string type_url) : type_url_(type_url) {}
  ResourceNameDiff() {}
};

class DeltaSubscriptionState : public Logger::Loggable<Logger::Id::config> {
public:
  DeltaSubscriptionState(const std::string& type_url,
                         const std::vector<std::string>& resource_names,
                         SubscriptionCallbacks& callbacks, const LocalInfo::LocalInfo& local_info,
                         std::chrono::milliseconds init_fetch_timeout,
                         Event::Dispatcher& dispatcher, SubscriptionStats& stats)
      : type_url_(type_url), callbacks_(callbacks), local_info_(local_info),
        init_fetch_timeout_(init_fetch_timeout), stats_(stats) {
    // Start us off with the list of resources we're interested in. The first_request_of_new_stream_
    // version of populateRequestWithDiff() will know to put these names in the subscribe field, but
    // *not* the initial_resource_versions field, due to them all being in the waitingForServer
    // state.
    for (const auto& resource_name : resource_names) {
      setResourceWaitingForServer(resource_name);
    }
    buildCleanRequest();
    setInitFetchTimeout(dispatcher);

    // The attempt stat here is maintained for the purposes of having consistency between ADS and
    // individual DeltaSubscriptions. Since ADS is push based and muxed, the notion of an
    // "attempt" for a given xDS API combined by ADS is not really that meaningful.
    stats_.update_attempt_.inc();
  }

  void setInitFetchTimeout(Event::Dispatcher& dispatcher) {
    if (init_fetch_timeout_.count() > 0 && !init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_ = dispatcher.createTimer([this]() -> void {
        ENVOY_LOG(warn, "delta config: initial fetch timed out for {}", type_url_);
        callbacks_.onConfigUpdateFailed(nullptr);
      });
      init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
    }
  }

  void pause() {
    ENVOY_LOG(debug, "Pausing discovery requests for {}", type_url_);
    ASSERT(!paused_);
    paused_ = true;
  }

  void resume(absl::optional<ResourceNameDiff>& to_send_if_any) {
    ENVOY_LOG(debug, "Resuming discovery requests for {}", type_url_);
    ASSERT(paused_);
    paused_ = false;
    to_send_if_any.reset();
    to_send_if_any.swap(pending_);
  }

  void buildCleanRequest() {
    request_.Clear();
    request_.set_type_url(type_url_);
    request_.mutable_node()->MergeFrom(local_info_.node());
  }

  void updateResourceNamesAndBuildDiff(const std::vector<std::string>& resource_names,
                                       ResourceNameDiff& diff) {
    stats_.update_attempt_.inc();
    std::set_difference(resource_names.begin(), resource_names.end(), resource_names_.begin(),
                        resource_names_.end(), std::inserter(diff.added_, diff.added_.begin()));
    std::set_difference(resource_names_.begin(), resource_names_.end(), resource_names.begin(),
                        resource_names.end(), std::inserter(diff.removed_, diff.removed_.begin()));

    for (const auto& added : diff.added_) {
      setResourceWaitingForServer(added);
    }
    for (const auto& removed : diff.removed_) {
      lostInterestInResource(removed);
    }
  }

  const envoy::api::v2::DeltaDiscoveryRequest&
  populateRequestWithDiff(const ResourceNameDiff& diff) {
    request_.clear_resource_names_subscribe();
    request_.clear_resource_names_unsubscribe();
    std::copy(diff.added_.begin(), diff.added_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_subscribe()));
    std::copy(diff.removed_.begin(), diff.removed_.end(),
              Protobuf::RepeatedFieldBackInserter(request_.mutable_resource_names_unsubscribe()));

    if (first_request_of_new_stream_) {
      // initial_resource_versions "must be populated for first request in a stream".
      for (auto const& resource : resource_versions_) {
        // Populate initial_resource_versions with the resource versions we currently have.
        // Resources we are interested in, but are still waiting to get any version of from the
        // server, do not belong in initial_resource_versions. (But do belong in new subscriptions!)
        if (!resource.second.waitingForServer()) {
          (*request_.mutable_initial_resource_versions())[resource.first] =
              resource.second.version();
        }
        *request_.mutable_resource_names_subscribe()->Add() = resource.first;
      }
    }
    return request_;
  }

  void set_first_request_of_new_stream(bool val) { first_request_of_new_stream_ = val; }

  bool checkPausedDuringSendAttempt(const ResourceNameDiff& diff) {
    if (paused_) {
      pending_ = diff; // resume() is now set to send this request.
      return true;
    }
    return false;
  }

  void handleResponse(envoy::api::v2::DeltaDiscoveryResponse* message) {
    // We *always* copy the response's nonce into the next request, even if we're going to make that
    // request a NACK by setting error_detail.
    request_.set_response_nonce(message->nonce());
    try {
      disableInitFetchTimeoutTimer();
      callbacks_.onConfigUpdate(message->resources(), message->removed_resources(),
                                message->system_version_info());
      for (const auto& resource : message->resources()) {
        setResourceVersion(resource.name(), resource.version());
      }
      // If a resource is gone, there is no longer a meaningful version for it that makes sense to
      // provide to the server upon stream reconnect: either it will continue to not exist, in which
      // case saying nothing is fine, or the server will bring back something new, which we should
      // receive regardless (which is the logic that not specifying a version will get you).
      //
      // So, leave the version map entry present but blank. It will be left out of
      // initial_resource_versions messages, but will remind us to explicitly tell the server "I'm
      // cancelling my subscription" when we lose interest.
      for (const auto& resource_name : message->removed_resources()) {
        if (resource_names_.find(resource_name) != resource_names_.end()) {
          setResourceWaitingForServer(resource_name);
        }
      }
      stats_.update_success_.inc();
      stats_.update_attempt_.inc();
      stats_.version_.set(HashUtil::xxHash64(message->system_version_info()));
      ENVOY_LOG(debug, "Delta config for {} accepted with {} resources added, {} removed",
                type_url_, message->resources().size(), message->removed_resources().size());
    } catch (const EnvoyException& e) {
      // Note that error_detail being set is what indicates that a DeltaDiscoveryRequest is a NACK.
      ::google::rpc::Status* error_detail = request_.mutable_error_detail();
      error_detail->set_code(Grpc::Status::GrpcStatus::Internal);
      error_detail->set_message(e.what());

      disableInitFetchTimeoutTimer();
      stats_.update_rejected_.inc();
      ENVOY_LOG(warn, "delta config for {} rejected: {}", type_url_, e.what());
      stats_.update_attempt_.inc();
      callbacks_.onConfigUpdateFailed(&e);
    }
  }

  void handleEstablishmentFailure() {
    stats_.update_failure_.inc();
    stats_.update_attempt_.inc();
    callbacks_.onConfigUpdateFailed(nullptr);
  }

private:
  void disableInitFetchTimeoutTimer() {
    if (init_fetch_timeout_timer_) {
      init_fetch_timeout_timer_->disableTimer();
      init_fetch_timeout_timer_.reset();
    }
  }

  class ResourceVersion {
  public:
    explicit ResourceVersion(absl::string_view version) : version_(version) {}
    // Builds a ResourceVersion in the waitingForServer state.
    ResourceVersion() {}

    // If true, we currently have no version of this resource - we are waiting for the server to
    // provide us with one.
    bool waitingForServer() const { return version_ == absl::nullopt; }
    // Must not be called if waitingForServer() == true.
    std::string version() const {
      ASSERT(version_.has_value());
      return version_.value_or("");
    }

  private:
    absl::optional<std::string> version_;
  };

  // Use these helpers to avoid forgetting to update both at once.
  void setResourceVersion(const std::string& resource_name, const std::string& resource_version) {
    resource_versions_[resource_name] = ResourceVersion(resource_version);
    resource_names_.insert(resource_name);
  }

  void setResourceWaitingForServer(const std::string& resource_name) {
    resource_versions_[resource_name] = ResourceVersion();
    resource_names_.insert(resource_name);
  }

  void lostInterestInResource(const std::string& resource_name) {
    resource_versions_.erase(resource_name);
    resource_names_.erase(resource_name);
  }

  // A map from resource name to per-resource version. The keys of this map are exactly the resource
  // names we are currently interested in. Those in the waitingForServer state currently don't have
  // any version for that resource: we need to inform the server if we lose interest in them, but we
  // also need to *not* include them in the initial_resource_versions map upon a reconnect.
  std::unordered_map<std::string, ResourceVersion> resource_versions_;
  // The keys of resource_versions_. Only tracked separately because std::map does not provide an
  // iterator into just its keys, e.g. for use in std::set_difference.
  std::unordered_set<std::string> resource_names_;

  const std::string type_url_;
  SubscriptionCallbacks& callbacks_;
  const LocalInfo::LocalInfo& local_info_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;

  // Paused via pause()?
  bool paused_{};
  bool first_request_of_new_stream_{true};
  absl::optional<ResourceNameDiff> pending_;

  // Current state of the request we next intend to send. It is updated in various ways at various
  // times by this class's functions.
  //
  // A note on request_ vs the ResourceNameDiff queue: request_ is an actual DeltaDiscoveryRequest
  // proto, which is maintained/updated as the code runs, and is what is actually sent out by
  // sendDiscoveryRequest(). queueDiscoveryRequest() queues an item into GrpcStream's abstract queue
  // - in this case, a ResourceNameDiff. request_ tracks mechanical implementation details, whereas
  // the queue items track the resources to be requested. When it comes time to send a
  // DiscoveryRequest, the front of that queue is sent into sendDiscoveryRequest(), which uses the
  // item to finalize request_ for sending. So, think of the ResourceNameDiff queue as "rough
  // sketches" that actual requests will be based on.
  envoy::api::v2::DeltaDiscoveryRequest request_;

  SubscriptionStats& stats_;
};

} // namespace Config
} // namespace Envoy
