#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"

#include "common/config/context_provider_impl.h"
#include "common/config/version_converter.h"

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

} // namespace

class LocalInfoImpl : public LocalInfo {
public:
  LocalInfoImpl(const envoy::config::core::v3::Node& node,
                const Protobuf::RepeatedPtrField<std::string>& node_context_params,
                const Network::Address::InstanceConstSharedPtr& address,
                absl::string_view zone_name, absl::string_view cluster_name,
                absl::string_view node_name)
      : node_(buildLocalNode(node, zone_name, cluster_name, node_name)), address_(address),
        context_provider_(node_, node_context_params) {}

  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& zoneName() const override { return node_.locality().zone(); }
  const std::string& clusterName() const override { return node_.cluster(); }
  const std::string& nodeName() const override { return node_.id(); }
  const envoy::config::core::v3::Node& node() const override { return node_; }
  const Config::ContextProvider& contextProvider() const override { return context_provider_; }

private:
  const envoy::config::core::v3::Node node_;
  const Network::Address::InstanceConstSharedPtr address_;
  const Config::ContextProviderImpl context_provider_;
};

} // namespace LocalInfo
} // namespace Envoy
