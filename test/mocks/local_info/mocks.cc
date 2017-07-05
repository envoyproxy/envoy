#include "mocks.h"

#include "common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Return;
using testing::ReturnRef;

namespace LocalInfo {

MockLocalInfo::MockLocalInfo() : address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, zoneName()).WillByDefault(ReturnRef(zone_name_));
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, nodeName()).WillByDefault(ReturnRef(node_name_));
}

MockLocalInfo::~MockLocalInfo() {}

} // namespace LocalInfo
} // namespace Envoy
