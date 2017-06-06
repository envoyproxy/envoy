#pragma once

#include <string>

#include "envoy/local_info/local_info.h"

namespace Envoy {
namespace LocalInfo {

class LocalInfoImpl : public LocalInfo {
public:
  LocalInfoImpl(Network::Address::InstanceConstSharedPtr address, const std::string& zone_name,
                const std::string& cluster_name, const std::string& node_name)
      : address_(std::move(address)), zone_name_(zone_name), cluster_name_(cluster_name),
        node_name_(node_name) {}

  Network::Address::InstanceConstSharedPtr address() const override { return address_; }
  const std::string& zoneName() const override { return zone_name_; }
  const std::string& clusterName() const override { return cluster_name_; }
  const std::string& nodeName() const override { return node_name_; }

private:
  Network::Address::InstanceConstSharedPtr address_;
  const std::string zone_name_;
  const std::string cluster_name_;
  const std::string node_name_;
};

} // LocalInfo
} // Envoy
