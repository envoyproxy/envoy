#pragma once

#include <string>

#include "envoy/local_info/local_info.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace LocalInfo {

class MockLocalInfo : public LocalInfo {
public:
  MockLocalInfo();
  ~MockLocalInfo();

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(zoneName, std::string&());
  MOCK_CONST_METHOD0(clusterName, std::string&());
  MOCK_CONST_METHOD0(nodeName, std::string&());
  MOCK_CONST_METHOD0(node, envoy::api::v2::Node&());

  Network::Address::InstanceConstSharedPtr address_;
  std::string zone_name_{"zone_name"};
  std::string cluster_name_{"cluster_name"};
  std::string node_name_{"node_name"};
  // TODO(htuch): Make this behave closer to the real implementation, with the various property
  // methods using node_ as the source of truth.
  envoy::api::v2::Node node_;
};

} // namespace LocalInfo
} // namespace Envoy
