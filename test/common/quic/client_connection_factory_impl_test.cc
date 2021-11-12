#include "source/common/quic/client_connection_factory_impl.h"
#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"

using testing::Return;

namespace Envoy {
namespace Quic {

class QuicNetworkConnectionTest : public Event::TestUsingSimulatedTime,
                                  public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  void initialize() {
    test_address_ = Network::Utility::resolveUrl(
        absl::StrCat("tcp://", Network::Test::getLoopbackAddressUrlString(GetParam()), ":30"));
    Ssl::ClientContextSharedPtr context{new Ssl::MockClientContext()};
    EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _, _))
        .WillOnce(Return(context));
    factory_ = std::make_unique<Quic::QuicClientTransportSocketFactory>(
        std::unique_ptr<Envoy::Ssl::ClientContextConfig>(
            new NiceMock<Ssl::MockClientContextConfig>),
        context_);
  }

  uint32_t highWatermark(EnvoyQuicClientSession* session) {
    return session->write_buffer_watermark_simulation_.highWatermark();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{new NiceMock<Upstream::MockHost>};
  NiceMock<Random::MockRandomGenerator> random_;
  Upstream::ClusterConnectivityState state_;
  Network::Address::InstanceConstSharedPtr test_address_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  std::unique_ptr<Quic::QuicClientTransportSocketFactory> factory_;
  Stats::IsolatedStoreImpl store_;
  QuicStatNames quic_stat_names_{store_.symbolTable()};
};

TEST_P(QuicNetworkConnectionTest, BufferLimits) {
  initialize();

  quic::QuicConfig config;
  PersistentQuicInfoImpl info{dispatcher_, *factory_, simTime(), test_address_, config, 45};

  std::unique_ptr<Network::ClientConnection> client_connection = createQuicNetworkConnection(
      info, dispatcher_, test_address_, test_address_, quic_stat_names_, store_);
  EnvoyQuicClientSession* session = static_cast<EnvoyQuicClientSession*>(client_connection.get());
  session->Initialize();
  client_connection->connect();
  EXPECT_TRUE(client_connection->connecting());
  ASSERT(session != nullptr);
  EXPECT_EQ(highWatermark(session), 45);
  EXPECT_EQ(absl::nullopt, session->unixSocketPeerCredentials());
  EXPECT_EQ(absl::nullopt, session->lastRoundTripTime());
  client_connection->close(Network::ConnectionCloseType::NoFlush);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicNetworkConnectionTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));
} // namespace Quic
} // namespace Envoy
