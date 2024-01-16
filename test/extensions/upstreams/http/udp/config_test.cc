#include "source/extensions/upstreams/http/udp/config.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

using ::testing::NiceMock;
using ::testing::Return;

class UdpGenericConnPoolFactoryTest : public ::testing::Test {
public:
  UdpGenericConnPoolFactoryTest() = default;

protected:
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  Upstream::ResourcePriority priority_ = Upstream::ResourcePriority::Default;
  Upstream::HostConstSharedPtr host_;
  UdpGenericConnPoolFactory factory_;
};

TEST_F(UdpGenericConnPoolFactoryTest, CreateValidUdpConnPool) {
  auto host = std::make_shared<Envoy::Upstream::MockHost>();
  EXPECT_CALL(thread_local_cluster_.lb_, chooseHost).WillOnce(Return(host));
  EXPECT_TRUE(factory_.createGenericConnPool(thread_local_cluster_,
                                             Router::GenericConnPoolFactory::UpstreamProtocol::UDP,
                                             priority_, Envoy::Http::Protocol::Http2, nullptr));
}

TEST_F(UdpGenericConnPoolFactoryTest, CreateInvalidUdpConnPool) {
  EXPECT_CALL(thread_local_cluster_.lb_, chooseHost).WillOnce(Return(nullptr));
  EXPECT_FALSE(factory_.createGenericConnPool(thread_local_cluster_,
                                              Router::GenericConnPoolFactory::UpstreamProtocol::UDP,
                                              priority_, Envoy::Http::Protocol::Http2, nullptr));
}

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
