#include "common/upstream/egds_api_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

EgdsApiImpl::EgdsApiImpl(
    EndpointGroupsManager& endpoint_group_mananger,
    ProtobufMessage::ValidationVisitor& validation_visitor,
    Config::SubscriptionFactory& subscription_factory, Stats::Scope& scope,
    const Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::Egds>& egds_list)
    : endpoint_group_mananger_(endpoint_group_mananger), validation_visitor_(validation_visitor),
      scope_(scope.createScope("egds.")), stats_({ALL_EGDS_STATS(POOL_COUNTER(*scope_))}) {
  std::unordered_map<envoy::config::core::v3::ConfigSource, std::set<std::string>, MessageUtil,
                     MessageUtil>
      config_source_map;
  for (auto& egds : egds_list) {
    if (config_source_map.count(egds.config_source())) {
      config_source_map[egds.config_source()].insert(egds.endpoint_group_name());
    } else {
      config_source_map[egds.config_source()] = {egds.endpoint_group_name()};
    }
  }

  for (auto& pair : config_source_map) {
    auto subscription = subscription_factory.subscriptionFromConfigSource(
        pair.first,
        Grpc::Common::typeUrl(
            envoy::config::endpoint::v3::EndpointGroup().GetDescriptor()->full_name()),
        *scope_, *this);
    subscription_map_.emplace(std::move(subscription), std::move(pair.second));
  }
}

void EgdsApiImpl::initialize(std::function<void()> initialization_fail_callback) {
  initialization_fail_callback_ = initialization_fail_callback;
  for (const auto& pair : subscription_map_) {
    pair.first->start(pair.second);
  }
}

void EgdsApiImpl::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                 const std::string& version_info) {
  if (resources.empty()) {
    stats_.update_empty_.inc();
    ENVOY_LOG(debug, "egds: empty resource");
    return;
  }

  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_add_repeated;
  for (const auto& endpoint_group_blob : resources) {
    auto endpoint_group =
        MessageUtil::anyConvert<envoy::config::endpoint::v3::EndpointGroup>(endpoint_group_blob);
    if (endpoint_group.endpoints().empty() && endpoint_group.named_endpoints().empty()) {
      *to_remove_repeated.Add() = endpoint_group.name();
      continue;
    }

    auto* to_add = to_add_repeated.Add();
    to_add->set_name(endpoint_group.name());
    to_add->set_version(version_info);
    to_add->mutable_resource()->PackFrom(endpoint_group);
  }

  onConfigUpdate(to_add_repeated, to_remove_repeated, version_info);
}

void EgdsApiImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& version_info) {
  std::vector<std::string> exception_msgs;
  std::unordered_set<std::string> endpoint_group_names;
  bool any_applied = false;
  std::vector<envoy::config::endpoint::v3::EndpointGroup> added;
  std::vector<std::string> removed;
  for (const auto& resource : added_resources) {
    envoy::config::endpoint::v3::EndpointGroup endpoint_group;
    try {
      endpoint_group =
          MessageUtil::anyConvert<envoy::config::endpoint::v3::EndpointGroup>(resource.resource());
      MessageUtil::validate(endpoint_group, validation_visitor_);
      if (!endpoint_group_names.insert(endpoint_group.name()).second) {
        throw EnvoyException(
            fmt::format("duplicate endpoint group {} found", endpoint_group.name()));
      }

      ENVOY_LOG(debug, "egds: added endpoint group '{}'", endpoint_group.name());
      added.emplace_back(endpoint_group);
    } catch (const EnvoyException& e) {
      exception_msgs.push_back(fmt::format("{}: {}", endpoint_group.name(), e.what()));
    }
  }

  ENVOY_LOG(debug, "egds: removed endpoint group '{}'", removed_resources.size());
  removed.insert(removed.end(), removed_resources.begin(), removed_resources.end());

  any_applied = endpoint_group_mananger_.batchUpdateEndpointGroup(added, removed, version_info);
  if (any_applied) {
    system_version_info_ = version_info;
  }

  if (!exception_msgs.empty()) {
    throw EnvoyException(fmt::format("Error adding/updating endpoint group(s) {}",
                                     absl::StrJoin(exception_msgs, ", ")));
  }

  stats_.config_reload_.inc();
}

void EgdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  initialization_fail_callback_();
}

} // namespace Upstream
} // namespace Envoy
