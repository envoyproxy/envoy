#include "common/config/ads_api_impl.h"

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

// TODO(htuch): describe the ADS protocol

AdsApiImpl::AdsApiImpl(const envoy::api::v2::Node& node,
                       const envoy::api::v2::ApiConfigSource& ads_config,
                       Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher)
    : node_(node), service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                       "envoy.api.v2.AggregatedDiscoveryService.StreamAggregatedResources")) {
  if (ads_config.cluster_name().empty()) {
    ENVOY_LOG(debug, "No ADS clusters defined, ADS will not be initialized.");
    return;
  }
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  if (ads_config.cluster_name().size() != 1) {
    // TODO(htuch): Add support for multiple clusters, #1170.
    throw EnvoyException(
        "envoy::api::v2::ApiConfigSource must have a singleton cluster name specified");
  }
  async_client_.reset(new Grpc::AsyncClientImpl<envoy::api::v2::DiscoveryRequest,
                                                envoy::api::v2::DiscoveryResponse>(
      cluster_manager, ads_config.cluster_name()[0]));
}

AdsApiImpl::~AdsApiImpl() {
  for (auto watches : watches_) {
    for (auto watch : watches.second) {
      watch->inserted_ = false;
    }
  }
}

void AdsApiImpl::start() {
  if (async_client_) {
    establishNewStream();
  }
}

void AdsApiImpl::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void AdsApiImpl::establishNewStream() {
  // TODO(htuch): stats
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  for (const auto type_url : subscriptions_) {
    sendDiscoveryRequest(requests_[type_url]);
  }
}

void AdsApiImpl::sendDiscoveryRequest(const envoy::api::v2::DiscoveryRequest& request) {
  if (stream_ == nullptr) {
    return;
  }
  stream_->sendMessage(request, false);
}

void AdsApiImpl::handleFailure() {
  for (auto watches : watches_) {
    for (auto watch : watches.second) {
      watch->callbacks_.onConfigUpdateFailed(nullptr);
    }
  }
  setRetryTimer();
}

AdsWatchPtr AdsApiImpl::subscribe(const std::string& type_url,
                                  const std::vector<std::string>& resources,
                                  AdsCallbacks& callbacks) {
  auto watch =
      std::unique_ptr<AdsWatchImpl>(new AdsWatchImpl(resources, callbacks, watches_[type_url]));
  ENVOY_LOG(debug, "ADS subscribe for " + type_url);

  // Lazily kick off the requests based on first subscription. This has the
  // convenient side-effect that we order messages on the channel based on
  // Envoy's internal dependency ordering.
  if (requests_.count(type_url) == 0) {
    requests_[type_url].set_type_url(type_url);
    requests_[type_url].mutable_node()->CopyFrom(node_);
    subscriptions_.push_front(type_url);
    sendDiscoveryRequest(requests_[type_url]);
  }

  return watch;
}

void AdsApiImpl::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void AdsApiImpl::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void AdsApiImpl::onReceiveMessage(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message) {
  const std::string& type_url = message->type_url();
  ENVOY_LOG(debug, "Received ADS message for {}", type_url);
  try {
    // To avoid O(n^2) explosion (e.g. when we have 1000s of EDS watches), we
    // build a map here from resource name to resource and then walk watches_.
    // TODO(htuch): Reconsider implementation data structure for watches to make lookups of resource
    // -> watches O(1), to avoid doing this crap.
    std::unordered_map<std::string, ProtobufWkt::Any> resources;
    for (const auto& resource : message->resources()) {
      if (type_url != resource.type_url()) {
        throw EnvoyException(fmt::format("{} does not match {} type URL is DiscoveryResponse {}",
                                         resource.type_url(), type_url, message->DebugString()));
      }
      resources.emplace(Utility::resourceName(resource), resource);
    }
    for (auto watch : watches_[type_url]) {
      if (watch->resources_.empty()) {
        watch->callbacks_.onConfigUpdate(message->resources());
        continue;
      }
      Protobuf::RepeatedPtrField<ProtobufWkt::Any> found_resources;
      for (auto watched_resource_name : watch->resources_) {
        auto it = resources.find(watched_resource_name);
        if (it != resources.end()) {
          found_resources.Add()->CopyFrom(it->second);
        }
      }
      if (!found_resources.empty()) {
        watch->callbacks_.onConfigUpdate(found_resources);
      }
    }
    requests_[type_url].set_version_info(message->version_info());
  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "ADS config for {} update rejected: {}", message->type_url(), e.what());
    for (auto watch : watches_[type_url]) {
      watch->callbacks_.onConfigUpdateFailed(&e);
    }
  }
  sendDiscoveryRequest(requests_[type_url]);
}

void AdsApiImpl::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void AdsApiImpl::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "ADS config stream closed: {}, {}", status, message);
  stream_ = nullptr;
  handleFailure();
}

} // namespace Config
} // namespace Envoy
