#include "source/common/listener_manager/fcds_api.h"

#include "envoy/config/listener/v3/listener_components.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"

namespace Envoy {
namespace Server {

FcdsApiImpl::FcdsApiImpl(const envoy::config::listener::v3::Listener::FcdsConfig& fcds_config,
                         const std::string& filter_chain_name,
                         FcdsSharedFilterChainManager& shared_manager, Upstream::ClusterManager& cm,
                         Stats::Scope& scope,
                         ProtobufMessage::ValidationVisitor& validation_visitor)
    : fcds_config_(fcds_config), filter_chain_name_(filter_chain_name),
      shared_manager_(shared_manager), cm_(cm),
      scope_(scope.createScope(fmt::format("listener_manager.fcds.{}.", filter_chain_name))),
      resource_type_helper_(validation_visitor, "name") {}

absl::Status FcdsApiImpl::start() {
  ENVOY_LOG(debug, "FcdsApiImpl::start: starting dynamic subscription for resource: {}",
            filter_chain_name_);
  if (!subscription_) {
    const auto resource_name = resource_type_helper_.getResourceName();
    auto subscription_or_error = cm_.subscriptionFactory().subscriptionFromConfigSource(
        fcds_config_.config_source(), Grpc::Common::typeUrl(resource_name),
        *scope_, *this, resource_type_helper_.resourceDecoder(), {});
    RETURN_IF_NOT_OK(subscription_or_error.status());
    subscription_ = std::move(subscription_or_error.value());
  }
  subscription_started_ = true;
  subscription_->start({filter_chain_name_});
  return absl::OkStatus();
}

absl::Status FcdsApiImpl::subscribeClient(FilterChainUpdateCallbacks& callbacks,
                                          Init::TargetImpl& init_target) {
  ENVOY_LOG(debug, "FcdsApiImpl::subscribeClient: subscribing client for: {}", filter_chain_name_);
  clients_.push_back({&callbacks, &init_target, false});

  if (!subscription_started_) {
    RETURN_IF_NOT_OK(start());
  }

  // Check if compiled and cached in the subscription
  if (filter_chain_ != nullptr) {
    ENVOY_LOG(
        debug,
        "FcdsApiImpl::subscribeClient: cached filter chain found, notifying client immediately");
    RETURN_IF_NOT_OK(callbacks.onFilterChainUpdate({filter_chain_}, {}));
    init_target.ready();
    clients_.back().init_target_ready_ = true;
  }
  return absl::OkStatus();
}

void FcdsApiImpl::unsubscribeClient(FilterChainUpdateCallbacks& callbacks) {
  auto it = std::remove_if(clients_.begin(), clients_.end(), [&callbacks](const Client& client) {
    return client.callbacks_ == &callbacks;
  });
  if (it != clients_.end()) {
    clients_.erase(it, clients_.end());
  }
}

absl::Status
FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            const std::string& system_version_info) {
  ENVOY_LOG(info, "fcds: config update received for name {}: added/updated: {}, removed: {}",
            filter_chain_name_, added_resources.size(), removed_resources.size());

  // 1. Compile the added/updated filter chain
  Network::DrainableFilterChainSharedPtr compiled_filter_chain = nullptr;
  for (const auto& resource : added_resources) {
    const envoy::config::listener::v3::FilterChain& filter_chain =
        Envoy::Protobuf::DynamicCastMessage<envoy::config::listener::v3::FilterChain>(
            resource.get().resource());
    auto result = shared_manager_.createOrUpdateSharedFilterChain(filter_chain);
    if (!result.ok()) {
      return result.status();
    }
    compiled_filter_chain = result.value();
  }

  if (compiled_filter_chain != nullptr) {
    filter_chain_ = compiled_filter_chain;
  } else if (!removed_resources.empty()) {
    filter_chain_ = nullptr;
  }

  std::vector<Network::DrainableFilterChainSharedPtr> added_or_updated;
  if (filter_chain_ != nullptr) {
    added_or_updated.push_back(filter_chain_);
  }

  std::vector<std::string> removed;
  for (const auto& removed_resource : removed_resources) {
    removed.push_back(removed_resource);
  }

  // 2. Notify each client with the runtime filter chain objects
  for (auto& client : clients_) {
    RETURN_IF_NOT_OK(client.callbacks_->onFilterChainUpdate(added_or_updated, removed));

    // 3. Mark client ready if cached
    if (!client.init_target_ready_ && filter_chain_ != nullptr) {
      client.init_target_->ready();
      client.init_target_ready_ = true;
    }
  }

  system_version_info_ = system_version_info;
  return absl::OkStatus();
}

absl::Status FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                         const std::string& version_info) {
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  bool found = false;
  for (const auto& resource : resources) {
    if (resource.get().name() == filter_chain_name_) {
      found = true;
      break;
    }
  }
  if (!found) {
    removed_resources.Add(std::string(filter_chain_name_));
  }
  return onConfigUpdate(resources, removed_resources, version_info);
}

void FcdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException* e) {
  ENVOY_LOG(warn, "fcds: update failed due to reason: {}", static_cast<int>(reason));
  if (e != nullptr) {
    ENVOY_LOG(warn, "fcds: failure details: {}", e->what());
  }
}

} // namespace Server
} // namespace Envoy
