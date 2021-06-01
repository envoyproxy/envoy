#pragma once

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/config/mocks.h"

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
  MOCK_METHOD(const Stats::StatName&, zoneStatName, (), (const));
  MOCK_METHOD(envoy::config::core::v3::Node&, node, (), (const));
  MOCK_METHOD(Config::ContextProvider&, contextProvider, ());
  MOCK_METHOD(const Config::ContextProvider&, contextProvider, (), (const));

  const Stats::StatName& makeZoneStatName() const;

  Network::Address::InstanceConstSharedPtr address_;
  // TODO(htuch): Make this behave closer to the real implementation, with the various property
  // methods using node_ as the source of truth.
  envoy::config::core::v3::Node node_;
  mutable Stats::TestUtil::TestSymbolTable symbol_table_;
  mutable std::unique_ptr<Stats::StatNameManagedStorage> zone_stat_name_storage_;
  mutable Stats::StatName zone_stat_name_;
  testing::NiceMock<Config::MockContextProvider> context_provider_;
};

} // namespace LocalInfo
} // namespace Envoy
