#include "source/extensions/clusters/common/logical_host.h"

#include "test/mocks/network/transport_socket.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
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
  description_.metadata();
  description_.priority();
  EXPECT_EQ(nullptr, description_.healthCheckAddress());

  // Pass through functions
  EXPECT_CALL(*mock_host_, transportSocketFactory());
  description_.transportSocketFactory();

  EXPECT_CALL(*mock_host_, canCreateConnection(_));
  description_.canCreateConnection(Upstream::ResourcePriority::Default);

  EXPECT_CALL(*mock_host_, loadMetricStats());
  description_.loadMetricStats();

  EXPECT_CALL(*mock_host_, addressList())
      .WillOnce(Return(std::make_shared<Upstream::HostDescription::AddressVector>()));
  description_.addressList();

  const envoy::config::core::v3::Metadata metadata;
  const envoy::config::cluster::v3::Cluster cluster;
  Network::MockTransportSocketFactory socket_factory;
  EXPECT_CALL(*mock_host_, resolveTransportSocketFactory(_, _)).WillOnce(ReturnRef(socket_factory));
  description_.resolveTransportSocketFactory(address_, &metadata);

  description_.canary(false);
  description_.priority(0);
  description_.metadata(nullptr);
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
