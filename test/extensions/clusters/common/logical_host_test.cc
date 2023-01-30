#include "source/extensions/clusters/common/logical_host.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <chrono>

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

  EXPECT_CALL(*mock_host_, lastTrafficPassTime());
  description_.lastTrafficPassTime();

  EXPECT_CALL(*mock_host_, lastTrafficPassTime2xx());
  description_.lastTrafficPassTime2xx();

  EXPECT_CALL(*mock_host_, lastTrafficPassTimeGrpc());
  description_.lastTrafficPassTimeGrpc();

  EXPECT_CALL(*mock_host_, setLastTrafficTimeTcpSuccess(_));
  description_.setLastTrafficTimeTcpSuccess( std::chrono::steady_clock::now());

  EXPECT_CALL(*mock_host_, setLastTrafficTimeHttp2xx(_));
  description_.setLastTrafficTimeHttp2xx(std::chrono::steady_clock::now());

  EXPECT_CALL(*mock_host_, setLastTrafficTimeGrpcSuccess(_));
  description_.setLastTrafficTimeGrpcSuccess(std::chrono::steady_clock::now());

  std::vector<Network::Address::InstanceConstSharedPtr> address_list;
  EXPECT_CALL(*mock_host_, addressList()).WillOnce(ReturnRef(address_list));
  description_.addressList();
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
