#pragma once

#include <string>

#include "envoy/config/core/v3alpha/base.pb.h"
#include "envoy/local_info/local_info.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace LocalInfo {

class MockLocalInfo : public LocalInfo {
public:
  MockLocalInfo();
  ~MockLocalInfo() override;

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(zoneName, const std::string&());
  MOCK_CONST_METHOD0(clusterName, const std::string&());
  MOCK_CONST_METHOD0(nodeName, const std::string&());
  MOCK_CONST_METHOD0(node, envoy::config::core::v3alpha::Node&());

  Network::Address::InstanceConstSharedPtr address_;
  // TODO(htuch): Make this behave closer to the real implementation, with the various property
  // methods using node_ as the source of truth.
  envoy::config::core::v3alpha::Node node_;
};

} // namespace LocalInfo
} // namespace Envoy
