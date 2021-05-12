#include "common/http/http3/conn_pool.h"
#include "common/quic/quic_transport_socket_factory.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Http {
namespace Http3 {

class Http3ConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  void initialize() {
    EXPECT_CALL(*mock_host, address()).WillRepeatedly(Return(test_address_));
    EXPECT_CALL(*mock_host, transportSocketFactory()).WillRepeatedly(testing::ReturnRef(factory_));
    new Event::MockSchedulableCallback(&dispatcher_);
    Network::ConnectionSocket::OptionsSharedPtr options;
    Network::TransportSocketOptionsSharedPtr transport_options;
    pool_ = allocateConnPool(dispatcher_, random_, host_, Upstream::ResourcePriority::Default,
                             options, transport_options, state_, simTime());
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::MockHost* mock_host = new NiceMock<Upstream::MockHost>;
  Upstream::HostSharedPtr host_{mock_host};
  NiceMock<Random::MockRandomGenerator> random_;
  Upstream::ClusterConnectivityState state_;
  Network::Address::InstanceConstSharedPtr test_address_ =
      Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  Quic::QuicClientTransportSocketFactory factory_{
      std::unique_ptr<Envoy::Ssl::ClientContextConfig>(new NiceMock<Ssl::MockClientContextConfig>),
      context_};
  ConnectionPool::InstancePtr pool_;
};

TEST_F(Http3ConnPoolImplTest, CreationWithBufferLimits) {
  EXPECT_CALL(mock_host->cluster_, perConnectionBufferLimitBytes);
  initialize();
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
