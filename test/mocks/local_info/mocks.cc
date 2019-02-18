#include "mocks.h"

#include "common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace LocalInfo {

MockLocalInfo::MockLocalInfo() : address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  node_.set_id("node_name");
  node_.set_cluster("cluster_name");
  node_.mutable_locality()->set_zone("zone_name");
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, zoneName()).WillByDefault(Invoke([this] { return node_.locality().zone(); }));
  ON_CALL(*this, clusterName()).WillByDefault(Invoke([this] { return node_.cluster(); }));
  ON_CALL(*this, nodeName()).WillByDefault(Invoke([this] { return node_.id(); }));
  ON_CALL(*this, node()).WillByDefault(ReturnRef(node_));
}

MockLocalInfo::~MockLocalInfo() {}

} // namespace LocalInfo
} // namespace Envoy
