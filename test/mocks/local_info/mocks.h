#pragma once

#include <string>

#include "envoy/local_info/local_info.h"

#include "gmock/gmock.h"

namespace Lyft {
namespace LocalInfo {

class MockLocalInfo : public LocalInfo {
public:
  MockLocalInfo();
  ~MockLocalInfo();

  MOCK_CONST_METHOD0(address, Network::Address::InstanceConstSharedPtr());
  MOCK_CONST_METHOD0(zoneName, std::string&());
  MOCK_CONST_METHOD0(clusterName, std::string&());
  MOCK_CONST_METHOD0(nodeName, std::string&());

  Network::Address::InstanceConstSharedPtr address_;
  std::string zone_name_{"zone_name"};
  std::string cluster_name_{"cluster_name"};
  std::string node_name_{"node_name"};
};

} // LocalInfo
} // Lyft