#include "source/common/listener_manager/fcds_api.h"

#include "envoy/config/listener/v3/listener_components.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"

namespace Envoy {
namespace Server {

FcdsApiImpl::FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
                         const std::string& filter_chain_name,
                         FilterChainUpdateCallbacks& callbacks, Upstream::ClusterManager& cm,
                         Stats::Scope& scope,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         absl::Status& creation_status)
    : fcds_config_(fcds_config), filter_chain_name_(filter_chain_name), callbacks_(callbacks),
      scope_(scope.createScope(absl::StrCat(filter_chain_name, "."))),
      resource_type_helper_(validation_visitor, "name"),
      init_target_(absl::StrCat("FCDS init ", filter_chain_name_), [this]() { start(); }) {
  const auto resource_name = resource_type_helper_.getResourceName();
  auto subscription_or_error = cm.subscriptionFactory().subscriptionFromConfigSource(
      fcds_config_, Grpc::Common::typeUrl(resource_name), *scope_, *this,
      resource_type_helper_.resourceDecoder(), {});
  SET_AND_RETURN_IF_NOT_OK(subscription_or_error.status(), creation_status);
  subscription_ = std::move(subscription_or_error).value();
}

FcdsApiImpl::~FcdsApiImpl() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();
}

void FcdsApiImpl::start() {
  if (!started_) {
    ENVOY_LOG(debug, "starting FCDS subscription for resource: {}", filter_chain_name_);
    started_ = true;
    subscription_->start({filter_chain_name_});
  }
}

void FcdsApiImpl::setFilterChain(Network::DrainableFilterChainSharedPtr&& filter_chain) {
  filter_chain_ = std::move(filter_chain);
  warming_ = false;
  init_target_.ready();
}

absl::Status
FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            const std::string& system_version_info) {
  ENVOY_LOG(info, "fcds: config update received for name {}: added/updated: {}, removed: {}",
            filter_chain_name_, added_resources.size(), removed_resources.size());
  OptRef<const FilterChainProto> updated_or_removed;
  if (added_resources.size() == 1) {
    if (removed_resources.size() > 0) {
      return absl::InvalidArgumentError(
          "Invalid FCDS update: removed and updated the same resource");
    }
    updated_or_removed = makeOptRef(
        Protobuf::DynamicCastMessage<FilterChainProto>(added_resources[0].get().resource()));
    if (updated_or_removed->name() != filter_chain_name_) {
      return absl::InvalidArgumentError(
          "Invalid FCDS update: unexpected updated filter chain name");
    }
  } else {
    if (removed_resources.size() != 1 || added_resources.size() != 0) {
      return absl::InvalidArgumentError("Invalid FCDS update: removed and added the same resource");
    }
    if (removed_resources[0] != filter_chain_name_) {
      return absl::InvalidArgumentError(
          "Invalid FCDS update: unexpected removed filter chain name");
    }
  }
  if (updated_or_removed && config_ &&
      Protobuf::util::MessageDifferencer::Equals(*updated_or_removed, *config_)) {
    ENVOY_LOG(debug, "fcds: skip update for name {}", filter_chain_name_);
    return absl::OkStatus();
  }
  system_version_info_ = system_version_info;
  if (updated_or_removed) {
    RETURN_IF_NOT_OK(callbacks_.onFilterChainUpdated(*updated_or_removed));
    // Delay readiness until the filter chain runtime instance is constructed.
    config_ = *updated_or_removed;
    warming_ = true;
  } else {
    callbacks_.onFilterChainRemoved(std::move(filter_chain_));
    filter_chain_ = nullptr;
    config_ = {};
    warming_ = false;
    init_target_.ready();
  }
  return absl::OkStatus();
}

absl::Status FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                         const std::string& version_info) {
  for (const auto& resource : resources) {
    if (resource.get().name() == filter_chain_name_) {
      return onConfigUpdate({resource}, {}, version_info);
    }
  }
  Protobuf::RepeatedPtrField<std::string> removed;
  removed.Add(std::string(filter_chain_name_));
  return onConfigUpdate({}, removed, version_info);
}

void FcdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException* e) {
  ENVOY_LOG(warn, "fcds: update failed due to reason: {}", static_cast<int>(reason));
  if (e != nullptr) {
    ENVOY_LOG(warn, "fcds: failure details: {}", e->what());
  }
  init_target_.ready();
}

FcdsFilterChainFactoryContextImpl::FcdsFilterChainFactoryContextImpl(
    Server::Configuration::ServerFactoryContext& server_context,
    const FilterChainProto& filter_chain)
    : server_context_(server_context), scope_(server_context_.scope().createScope("")),
      prefixed_scope_(server_context_.scope().createScope(
          absl::StrCat("filter_chain.", filter_chain.name(), "."))),
      init_manager_(absl::StrCat("fcds_filter_chain ", filter_chain.name())) {}

bool FcdsFilterChainFactoryContextImpl::drainClose(Network::DrainDirection scope) const {
  return is_draining_.load() || server_context_.drainManager().drainClose(scope);
}

Network::DrainDecision& FcdsFilterChainFactoryContextImpl::drainDecision() { return *this; }

Init::Manager& FcdsFilterChainFactoryContextImpl::initManager() { return init_manager_; }

ProtobufMessage::ValidationVisitor& FcdsFilterChainFactoryContextImpl::messageValidationVisitor() {
  return server_context_.messageValidationVisitor();
}

Stats::Scope& FcdsFilterChainFactoryContextImpl::scope() { return *scope_; }
Stats::Scope& FcdsFilterChainFactoryContextImpl::prefixedScope() { return *prefixed_scope_; }

Configuration::ServerFactoryContext& FcdsFilterChainFactoryContextImpl::serverFactoryContext() {
  return server_context_;
}

envoy::config::core::v3::TrafficDirection FcdsFilterChainFactoryContextImpl::direction() const {
  return envoy::config::core::v3::TrafficDirection::UNSPECIFIED;
}
bool FcdsFilterChainFactoryContextImpl::isQuic() const { return false; }
bool FcdsFilterChainFactoryContextImpl::shouldBypassOverloadManager() const { return false; }

} // namespace Server
} // namespace Envoy
