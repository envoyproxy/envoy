#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace LocalInfo {

class MockLocalInfo : public LocalInfo {
public:
  MockLocalInfo();
  ~MockLocalInfo() override;

  MOCK_METHOD(Network::Address::InstanceConstSharedPtr, address, (), (const));
  MOCK_METHOD(const std::string&, zoneName, (), (const));
  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const std::string&, nodeName, (), (const));
  MOCK_METHOD(envoy::config::core::v3::Node&, node, (), (const));

  Network::Address::InstanceConstSharedPtr address_;
  // TODO(htuch): Make this behave closer to the real implementation, with the various property
  // methods using node_ as the source of truth.
  envoy::config::core::v3::Node node_;
};

} // namespace LocalInfo
} // namespace Envoy
