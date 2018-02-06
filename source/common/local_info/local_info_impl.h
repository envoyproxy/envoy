#pragma once

#include <string>

#include "envoy/local_info/local_info.h"

namespace Envoy {
namespace LocalInfo {

class LocalInfoImpl : public LocalInfo {
public:
  LocalInfoImpl(const envoy::api::v2::core::Node& node,
                Network::Address::InstanceConstSharedPtr address, const std::string zone_name,
                const std::string cluster_name, const std::string node_name)
      : node_(node), address_(address) {
    if (!zone_name.empty()) {
      node_.mutable_locality()->set_zone(zone_name);
    }
    if (!cluster_name.empty()) {
      node_.set_cluster(cluster_name);
    }
    if (!node_name.empty()) {
      node_.set_id(node_name);
    }
  }

  // TODO(PiotrSikora): Revert https://github.com/envoyproxy/envoy/pull/1500 once protobuf string
  // types converge.

  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string zoneName() const override { return node_.locality().zone(); }
  const std::string clusterName() const override { return node_.cluster(); }
  const std::string nodeName() const override { return node_.id(); }
  const envoy::api::v2::core::Node& node() const override { return node_; }

private:
  envoy::api::v2::core::Node node_;
  Network::Address::InstanceConstSharedPtr address_;
};

} // namespace LocalInfo
} // namespace Envoy
