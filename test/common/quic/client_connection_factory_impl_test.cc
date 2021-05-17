#include "common/quic/client_connection_factory_impl.h"
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
namespace Quic {

class QuicNetworkConnectionTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{new NiceMock<Upstream::MockHost>};
  NiceMock<Random::MockRandomGenerator> random_;
  Upstream::ClusterConnectivityState state_;
  Network::Address::InstanceConstSharedPtr test_address_ =
      Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  Quic::QuicClientTransportSocketFactory factory_{
      std::unique_ptr<Envoy::Ssl::ClientContextConfig>(new NiceMock<Ssl::MockClientContextConfig>),
      context_};

  uint32_t highWatermark(EnvoyQuicClientSession* session) {
    return session->write_buffer_watermark_simulation_.highWatermark();
  }
};

TEST_F(QuicNetworkConnectionTest, BufferLimits) {
  PersistentQuicInfoImpl info{dispatcher_, factory_, simTime(), test_address_, 45};

  std::unique_ptr<Network::ClientConnection> client_connection =
      createQuicNetworkConnection(info, dispatcher_, test_address_, test_address_);
  EnvoyQuicClientSession* session = static_cast<EnvoyQuicClientSession*>(client_connection.get());
  session->Initialize();
  client_connection->connect();
  ASSERT(session != nullptr);
  EXPECT_EQ(highWatermark(session), 45);
}

} // namespace Quic
} // namespace Envoy

/*
class Http3ConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
void initialize() {
EXPECT_CALL(mockHost(), address()).WillRepeatedly(Return(test_address_));
EXPECT_CALL(mockHost(), transportSocketFactory()).WillRepeatedly(testing::ReturnRef(factory_));
new Event::MockSchedulableCallback(&dispatcher_);
Network::ConnectionSocket::OptionsSharedPtr options;
Network::TransportSocketOptionsSharedPtr transport_options;
pool_ = allocateConnPool(dispatcher_, random_, host_, Upstream::ResourcePriority::Default,
options, transport_options, state_, simTime());
}

Upstream::MockHost& mockHost() { return static_cast<Upstream::MockHost&>(*host_); }

std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
Upstream::HostSharedPtr host_{new NiceMock<Upstream::MockHost>};
NiceMock<Random::MockRandomGenerator> random_;
Upstream::ClusterConnectivityState state_;
Network::Address::InstanceConstSharedPtr test_address_ =
Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
Quic::QuicClientTransportSocketFactory factory_{
std::unique_ptr<Envoy::Ssl::ClientContextConfig>(new NiceMock<Ssl::MockClientContextConfig>),
context_};
ConnectionPool::InstancePtr pool_;
*/
