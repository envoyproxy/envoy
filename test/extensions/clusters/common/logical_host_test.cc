#include <chrono>

#include "source/extensions/clusters/common/logical_host.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Clusters {

class RealHostDescription : public testing::Test {
public:
  Network::Address::InstanceConstSharedPtr address_ = nullptr;
  Upstream::MockHost* mock_host_{new NiceMock<Upstream::MockHost>()};
  Upstream::HostConstSharedPtr host_{mock_host_};
  Upstream::RealHostDescription description_{address_, host_};
};

TEST_F(RealHostDescription, UnitTest) {
  // No-op unit tests
  description_.canary();
  description_.canary(true);
  description_.metadata();
  description_.priority();
  description_.priority(0);
  EXPECT_EQ(nullptr, description_.healthCheckAddress());

  // Pass through functions
  EXPECT_CALL(*mock_host_, transportSocketFactory());
  description_.transportSocketFactory();

  EXPECT_CALL(*mock_host_, canCreateConnection(_));
  description_.canCreateConnection(Upstream::ResourcePriority::Default);

  EXPECT_CALL(*mock_host_, loadMetricStats());
  description_.loadMetricStats();

  EXPECT_CALL(*mock_host_, lastSuccessfulTrafficTime(_));
  description_.lastSuccessfulTrafficTime(envoy::data::core::v3::HealthCheckerType::TCP);

  EXPECT_CALL(*mock_host_, setLastSuccessfulTrafficTime(_, _));
  description_.setLastSuccessfulTrafficTime(
      envoy::data::core::v3::HealthCheckerType::TCP,
      std::chrono::steady_clock::now()); // NO_CHECK_FORMAT(real_time)

  std::vector<Network::Address::InstanceConstSharedPtr> address_list;
  EXPECT_CALL(*mock_host_, addressList()).WillOnce(ReturnRef(address_list));
  description_.addressList();
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
