#include "source/common/http/http3/conn_pool.h"
#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace Http3 {

TEST(Convert, Basic) {
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  quic::QuicConfig config;

  EXPECT_CALL(cluster_info, connectTimeout).WillOnce(Return(std::chrono::milliseconds(42)));
  auto* protocol_options = cluster_info.http3_options_.mutable_quic_protocol_options();
  protocol_options->mutable_max_concurrent_streams()->set_value(43);
  protocol_options->mutable_initial_stream_window_size()->set_value(65555);

  Http3ConnPoolImpl::setQuicConfigFromClusterConfig(cluster_info, config);

  EXPECT_EQ(config.max_time_before_crypto_handshake(), quic::QuicTime::Delta::FromMilliseconds(42));
  EXPECT_EQ(config.GetMaxBidirectionalStreamsToSend(),
            protocol_options->max_concurrent_streams().value());
  EXPECT_EQ(config.GetMaxUnidirectionalStreamsToSend(),
            protocol_options->max_concurrent_streams().value());
  EXPECT_EQ(config.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend(),
            protocol_options->initial_stream_window_size().value());
}

class Http3ConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  void initialize() {
    EXPECT_CALL(mockHost(), address()).WillRepeatedly(Return(test_address_));
    EXPECT_CALL(mockHost(), transportSocketFactory()).WillRepeatedly(testing::ReturnRef(factory_));
    new Event::MockSchedulableCallback(&dispatcher_);
    Network::ConnectionSocket::OptionsSharedPtr options;
    Network::TransportSocketOptionsConstSharedPtr transport_options;
    pool_ =
        allocateConnPool(dispatcher_, random_, host_, Upstream::ResourcePriority::Default, options,
                         transport_options, state_, simTime(), quic_stat_names_, store_);
  }

  Upstream::MockHost& mockHost() { return static_cast<Upstream::MockHost&>(*host_); }

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
  Stats::IsolatedStoreImpl store_;
  Quic::QuicStatNames quic_stat_names_{store_.symbolTable()};
  ConnectionPool::InstancePtr pool_;
};

TEST_F(Http3ConnPoolImplTest, CreationWithBufferLimits) {
  EXPECT_CALL(mockHost().cluster_, perConnectionBufferLimitBytes);
  initialize();
}

TEST_F(Http3ConnPoolImplTest, CreationWithConfig) {
  // Set a couple of options from setQuicConfigFromClusterConfig to make sure they are applied.
  auto* options = mockHost().cluster_.http3_options_.mutable_quic_protocol_options();
  options->mutable_max_concurrent_streams()->set_value(15);
  options->mutable_initial_stream_window_size()->set_value(65555);
  initialize();

  Quic::PersistentQuicInfoImpl& info = static_cast<Http3ConnPoolImpl*>(pool_.get())->quicInfo();
  EXPECT_EQ(info.quic_config_.GetMaxUnidirectionalStreamsToSend(),
            options->max_concurrent_streams().value());
  EXPECT_EQ(info.quic_config_.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend(),
            options->initial_stream_window_size().value());
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
