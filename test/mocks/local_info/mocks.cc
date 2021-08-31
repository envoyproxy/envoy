#include "mocks.h"

#include "source/common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Const;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace LocalInfo {

MockLocalInfo::MockLocalInfo() : address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  node_.set_id("node_name");
  node_.set_cluster("cluster_name");
  node_.mutable_locality()->set_zone("zone_name");
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, zoneName()).WillByDefault(ReturnRef(node_.locality().zone()));
  ON_CALL(*this, zoneStatName()).WillByDefault(ReturnRef(makeZoneStatName()));
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(node_.cluster()));
  ON_CALL(*this, nodeName()).WillByDefault(ReturnRef(node_.id()));
  ON_CALL(*this, node()).WillByDefault(ReturnRef(node_));
  ON_CALL(*this, contextProvider()).WillByDefault(ReturnRef(context_provider_));
  ON_CALL(Const(*this), contextProvider()).WillByDefault(ReturnRef(context_provider_));
}

MockLocalInfo::~MockLocalInfo() = default;

const Stats::StatName& MockLocalInfo::makeZoneStatName() const {
  if (zone_stat_name_storage_ == nullptr ||
      symbol_table_->toString(zone_stat_name_) != node_.locality().zone()) {
    zone_stat_name_storage_ =
        std::make_unique<Stats::StatNameManagedStorage>(node_.locality().zone(), *symbol_table_);
    zone_stat_name_ = zone_stat_name_storage_->statName();
  }
  return zone_stat_name_;
}

} // namespace LocalInfo
} // namespace Envoy
