#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"

#include "source/common/config/context_provider_impl.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace LocalInfo {

namespace {

const envoy::config::core::v3::Node buildLocalNode(const envoy::config::core::v3::Node& node,
                                                   absl::string_view zone_name,
                                                   absl::string_view cluster_name,
                                                   absl::string_view node_name) {
  envoy::config::core::v3::Node local_node;
  local_node.MergeFrom(node);
  if (!zone_name.empty()) {
    local_node.mutable_locality()->set_zone(std::string(zone_name));
  }
  if (!cluster_name.empty()) {
    local_node.set_cluster(std::string(cluster_name));
  }
  if (!node_name.empty()) {
    local_node.set_id(std::string(node_name));
  }
  return local_node;
}

const absl::string_view getZoneName(const envoy::config::core::v3::Node& node,
                                    absl::string_view zone_name) {
  if (zone_name.empty()) {
    return node.locality().zone();
  }
  return zone_name;
}

} // namespace

class LocalInfoImpl : public LocalInfo {
public:
  LocalInfoImpl(Stats::SymbolTable& symbol_table, const envoy::config::core::v3::Node& node,
                const Protobuf::RepeatedPtrField<std::string>& node_context_params,
                const Network::Address::InstanceConstSharedPtr& address,
                absl::string_view zone_name, absl::string_view cluster_name,
                absl::string_view node_name)
      : node_(buildLocalNode(node, zone_name, cluster_name, node_name)), address_(address),
        context_provider_(node_, node_context_params),
        zone_stat_name_storage_(getZoneName(node_, zone_name), symbol_table),
        zone_stat_name_(zone_stat_name_storage_.statName()),
        dynamic_update_callback_handle_(context_provider_.addDynamicContextUpdateCallback(
            [this](absl::string_view resource_type_url) {
              (*node_.mutable_dynamic_parameters())
                  [toStdStringView(resource_type_url)] // NOLINT(std::string_view)
                      .CopyFrom(context_provider_.dynamicContext(resource_type_url));
              return absl::OkStatus();
            })) {}

  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& zoneName() const override { return node_.locality().zone(); }
  const Stats::StatName& zoneStatName() const override { return zone_stat_name_; }
  const std::string& clusterName() const override { return node_.cluster(); }
  const std::string& nodeName() const override { return node_.id(); }
  const envoy::config::core::v3::Node& node() const override { return node_; }
  const Config::ContextProvider& contextProvider() const override { return context_provider_; }
  Config::ContextProvider& contextProvider() override { return context_provider_; }

private:
  envoy::config::core::v3::Node node_;
  const Network::Address::InstanceConstSharedPtr address_;
  Config::ContextProviderImpl context_provider_;
  const Stats::StatNameManagedStorage zone_stat_name_storage_;
  const Stats::StatName zone_stat_name_;
  Common::CallbackHandlePtr dynamic_update_callback_handle_;
};

} // namespace LocalInfo
} // namespace Envoy
