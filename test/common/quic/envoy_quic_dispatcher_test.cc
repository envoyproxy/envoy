#include <openssl/evp.h>

#include <memory>

#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_clock.h"
#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/server/configuration_impl.h"

#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_utils.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_dispatcher_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

namespace {
const size_t kNumSessionsToCreatePerLoopForTests = 16;
}

class EnvoyQuicDispatcherTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                protected Logger::Loggable<Logger::Id::main> {
public:
  EnvoyQuicDispatcherTest()
      : version_(GetParam()), api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        listen_socket_(std::make_unique<Network::NetworkListenSocket<
                           Network::NetworkSocketTrait<Network::Socket::Type::Datagram>>>(
            Network::Test::getCanonicalLoopbackAddress(version_), nullptr, /*bind*/ true)),
        connection_helper_(*dispatcher_), proof_source_(new TestProofSource()),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::unique_ptr<TestProofSource>(proof_source_),
                       quic::KeyExchangeSource::Default()),
        version_manager_(quic::CurrentSupportedHttp3Versions()),
        quic_version_(version_manager_.GetSupportedVersions()[0]),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
            POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "worker."),
            POOL_GAUGE_PREFIX(listener_config_.listenerScope(), "worker."))}),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()),
        connection_handler_(*dispatcher_, absl::nullopt),
        connection_id_generator_(quic::kQuicDefaultConnectionIdLength),
        envoy_quic_dispatcher_(
            &crypto_config_, quic_config_, &version_manager_,
            std::make_unique<EnvoyQuicConnectionHelper>(*dispatcher_),
            std::make_unique<EnvoyQuicAlarmFactory>(*dispatcher_, *connection_helper_.GetClock()),
            quic::kQuicDefaultConnectionIdLength, connection_handler_, listener_config_,
            listener_stats_, per_worker_stats_, *dispatcher_, *listen_socket_, quic_stat_names_,
            crypto_stream_factory_, connection_id_generator_),
        connection_id_(quic::test::TestConnectionId(1)),
        transport_socket_factory_(true, listener_config_.listenerScope(),
                                  std::make_unique<NiceMock<Ssl::MockServerContextConfig>>()) {
    auto writer = new testing::NiceMock<quic::test::MockPacketWriter>();
    envoy_quic_dispatcher_.InitializeWithWriter(writer);
    EXPECT_CALL(*writer, WritePacket(_, _, _, _, _, _))
        .WillRepeatedly(Return(quic::WriteResult(quic::WRITE_STATUS_OK, 0)));
    EXPECT_CALL(proof_source_->filterChain(), transportSocketFactory())
        .WillRepeatedly(ReturnRef(transport_socket_factory_));
  }

  void SetUp() override {
    // Advance time a bit because QuicTime regards 0 as uninitialized timestamp.
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(100), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(listener_config_, perConnectionBufferLimitBytes())
        .WillRepeatedly(Return(1024 * 1024));
  }

  void TearDown() override {
    quic::QuicBufferedPacketStore* buffered_packets =
        quic::test::QuicDispatcherPeer::GetBufferedPackets(&envoy_quic_dispatcher_);
    EXPECT_FALSE(buffered_packets->HasChlosBuffered());
    EXPECT_FALSE(buffered_packets->HasBufferedPackets(connection_id_));

    envoy_quic_dispatcher_.Shutdown();
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  void processValidChloPacket(const quic::QuicSocketAddress& peer_addr) {
    // Create a Quic Crypto or TLS1.3 CHLO packet.
    EnvoyQuicClock clock(*dispatcher_);
    Buffer::OwnedImpl payload =
        generateChloPacketToSend(quic_version_, quic_config_, connection_id_);
    Buffer::RawSliceVector slice = payload.getRawSlices();
    ASSERT(slice.size() == 1);
    auto encrypted_packet = std::make_unique<quic::QuicEncryptedPacket>(
        static_cast<char*>(slice[0].mem_), slice[0].len_);
    std::unique_ptr<quic::QuicReceivedPacket> received_packet =
        std::unique_ptr<quic::QuicReceivedPacket>(
            quic::test::ConstructReceivedPacket(*encrypted_packet, clock.Now()));

    envoy_quic_dispatcher_.ProcessPacket(
        envoyIpAddressToQuicSocketAddress(
            listen_socket_->connectionInfoProvider().localAddress()->ip()),
        peer_addr, *received_packet);
  }

  void processValidChloPacketAndCheckStatus(bool should_buffer) {
    quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                          ? quic::QuicIpAddress::Loopback4()
                                          : quic::QuicIpAddress::Loopback6(),
                                      54321);
    quic::QuicBufferedPacketStore* buffered_packets =
        quic::test::QuicDispatcherPeer::GetBufferedPackets(&envoy_quic_dispatcher_);
    if (!should_buffer) {
      // Set QuicDispatcher::new_sessions_allowed_per_event_loop_ to
      // |kNumSessionsToCreatePerLoopForTests| so that received CHLOs can be
      // processed immediately.
      envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
      EXPECT_FALSE(buffered_packets->HasChlosBuffered());
      EXPECT_FALSE(buffered_packets->HasBufferedPackets(connection_id_));
    }

    processValidChloPacket(peer_addr);
    if (should_buffer) {
      // Incoming CHLO packet is buffered, because ProcessPacket() is called before
      // ProcessBufferedChlos().
      EXPECT_TRUE(buffered_packets->HasChlosBuffered());
      EXPECT_TRUE(buffered_packets->HasBufferedPackets(connection_id_));

      // Process the buffered CHLO now.
      envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
    }

    EXPECT_FALSE(buffered_packets->HasChlosBuffered());
    EXPECT_FALSE(buffered_packets->HasBufferedPackets(connection_id_));

    // A new QUIC connection is created and its filter installed based on self and peer address.
    EXPECT_EQ(1u, envoy_quic_dispatcher_.NumSessions());
    const quic::QuicSession* session =
        quic::test::QuicDispatcherPeer::FindSession(&envoy_quic_dispatcher_, connection_id_);
    ASSERT(session != nullptr);
    EXPECT_TRUE(session->IsEncryptionEstablished());
    EXPECT_EQ(1u, connection_handler_.numConnections());
    auto envoy_connection = static_cast<const EnvoyQuicServerSession*>(session);
    EXPECT_EQ("test.example.com", envoy_connection->requestedServerName());
    EXPECT_EQ(peer_addr, envoyIpAddressToQuicSocketAddress(
                             envoy_connection->connectionInfoProvider().remoteAddress()->ip()));
    ASSERT(envoy_connection->connectionInfoProvider().localAddress() != nullptr);
    EXPECT_EQ(*listen_socket_->connectionInfoProvider().localAddress(),
              *envoy_connection->connectionInfoProvider().localAddress());
    EXPECT_EQ(64 * 1024, envoy_connection->max_inbound_header_list_size());
  }

  class PreparedFilterChainMocks {
  public:
    PreparedFilterChainMocks(EnvoyQuicDispatcherTest& test) {
      filter_factory_.push_back(
          std::make_unique<Config::TestExtensionConfigProvider<Network::FilterFactoryCb>>(
              [&](Network::FilterManager& filter_manager) {
                filter_manager.addReadFilter(read_filter_);
                read_filter_->callbacks_->connection().addConnectionCallbacks(
                    network_connection_callbacks_);
                read_filter_->callbacks_->connection().setConnectionStats(
                    {read_total_, read_current_, write_total_, write_current_, nullptr, nullptr});
              }));
      EXPECT_CALL(test.listener_config_, filterChainManager())
          .WillOnce(ReturnRef(filter_chain_manager_));
      EXPECT_CALL(filter_chain_manager_, findFilterChain(_, _))
          .WillOnce(Invoke(
              [&test](const Network::ConnectionSocket& socket, const StreamInfo::StreamInfo&) {
                EXPECT_EQ("h3", socket.requestedApplicationProtocols()[0]);
                EXPECT_EQ("test.example.com", socket.requestedServerName());
                return &test.proof_source_->filterChain();
              }));
      EXPECT_CALL(test.proof_source_->filterChain(), networkFilterFactories())
          .WillOnce(ReturnRef(filter_factory_));
      EXPECT_CALL(test.listener_config_, filterChainFactory()).Times(2u);
      EXPECT_CALL(test.listener_config_.filter_chain_factory_, createQuicListenerFilterChain(_))
          .WillRepeatedly(Return(true));
      EXPECT_CALL(test.listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
          .WillOnce(Invoke([](Network::Connection& connection,
                              const Filter::NetworkFilterFactoriesList& filter_factories) {
            EXPECT_EQ(1u, filter_factories.size());
            Server::Configuration::FilterChainUtility::buildFilterChain(connection,
                                                                        filter_factories);
            dynamic_cast<EnvoyQuicServerSession&>(connection)
                .set_max_inbound_header_list_size(64 * 1024);
            return true;
          }));
      EXPECT_CALL(*read_filter_, onNewConnection())
          // Stop iteration to avoid calling getRead/WriteBuffer().
          .WillOnce(Return(Network::FilterStatus::StopIteration));
      EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
    }

  private:
    Network::MockFilterChainManager filter_chain_manager_;
    std::shared_ptr<Network::MockReadFilter> read_filter_{new Network::MockReadFilter()};
    Network::MockConnectionCallbacks network_connection_callbacks_;
    testing::StrictMock<Stats::MockCounter> read_total_;
    testing::StrictMock<Stats::MockGauge> read_current_;
    testing::StrictMock<Stats::MockCounter> write_total_;
    testing::StrictMock<Stats::MockGauge> write_current_;

    Filter::NetworkFilterFactoriesList filter_factory_;
  };

  void processValidChloPacketAndInitializeFilters(bool should_buffer) {
    PreparedFilterChainMocks mocks(*this);
    processValidChloPacketAndCheckStatus(should_buffer);
    // Shutdown() to close the connection.
    envoy_quic_dispatcher_.Shutdown();
  }

protected:
  Network::Address::IpVersion version_;
  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::SocketPtr listen_socket_;
  EnvoyQuicConnectionHelper connection_helper_;
  TestProofSource* proof_source_;
  quic::QuicCryptoServerConfig crypto_config_;
  quic::QuicConfig quic_config_;
  quic::QuicVersionManager version_manager_;
  quic::ParsedQuicVersion quic_version_;
  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  Server::PerHandlerListenerStats per_worker_stats_;
  QuicStatNames quic_stat_names_;
  Server::ConnectionHandlerImpl connection_handler_;
  EnvoyQuicCryptoServerStreamFactoryImpl crypto_stream_factory_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_;
  EnvoyQuicDispatcher envoy_quic_dispatcher_;
  quic::QuicConnectionId connection_id_;
  QuicServerTransportSocketFactory transport_socket_factory_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicDispatcherTests, EnvoyQuicDispatcherTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(EnvoyQuicDispatcherTest, CreateNewConnectionUponCHLO) {
  processValidChloPacketAndInitializeFilters(false);
}

TEST_P(EnvoyQuicDispatcherTest, CloseConnectionDuringNetworkFilterInstallation) {
  Network::MockFilterChainManager filter_chain_manager;
  std::shared_ptr<Network::MockReadFilter> read_filter(new Network::MockReadFilter());
  Network::MockConnectionCallbacks network_connection_callbacks;
  testing::StrictMock<Stats::MockCounter> read_total;
  testing::StrictMock<Stats::MockGauge> read_current;
  testing::StrictMock<Stats::MockCounter> write_total;
  testing::StrictMock<Stats::MockGauge> write_current;

  Filter::NetworkFilterFactoriesList filter_factory;
  filter_factory.push_back(
      std::make_unique<Config::TestExtensionConfigProvider<Network::FilterFactoryCb>>(
          [&](Network::FilterManager& filter_manager) {
            filter_manager.addReadFilter(read_filter);
            read_filter->callbacks_->connection().addConnectionCallbacks(
                network_connection_callbacks);
            read_filter->callbacks_->connection().setConnectionStats(
                {read_total, read_current, write_total, write_current, nullptr, nullptr});
            // This will not close connection right away, but during processing the first packet.
            read_filter->callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
          }));

  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(&proof_source_->filterChain()));
  EXPECT_CALL(proof_source_->filterChain(), networkFilterFactories())
      .WillOnce(ReturnRef(filter_factory));
  EXPECT_CALL(listener_config_, filterChainFactory()).Times(2u);
  EXPECT_CALL(listener_config_.filter_chain_factory_, createQuicListenerFilterChain(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
      .WillOnce(Invoke([](Network::Connection& connection,
                          const Filter::NetworkFilterFactoriesList& filter_factories) {
        EXPECT_EQ(1u, filter_factories.size());
        Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
        return true;
      }));
  EXPECT_CALL(*read_filter, onNewConnection())
      // Stop iteration to avoid calling getRead/WriteBuffer().
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  // Set QuicDispatcher::new_sessions_allowed_per_event_loop_ to
  // |kNumSessionsToCreatePerLoopForTests| so that received CHLOs can be
  // processed immediately.
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);

  processValidChloPacket(peer_addr);
}

TEST_P(EnvoyQuicDispatcherTest, CloseConnectionDuringListenerFilterInstallation) {
  Network::MockFilterChainManager filter_chain_manager;

  EXPECT_CALL(listener_config_, filterChainFactory());
  // Failure in creating listener filter chain should pause network filter chain installation as
  // well.
  EXPECT_CALL(listener_config_.filter_chain_factory_, createQuicListenerFilterChain(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _)).Times(0u);

  // Set QuicDispatcher::new_sessions_allowed_per_event_loop_ to
  // |kNumSessionsToCreatePerLoopForTests| so that received CHLOs can be
  // processed immediately.
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);

  processValidChloPacket(peer_addr);
  EXPECT_EQ(0u, envoy_quic_dispatcher_.NumSessions());
}

TEST_P(EnvoyQuicDispatcherTest, CreateNewConnectionUponBufferedCHLO) {
  processValidChloPacketAndInitializeFilters(true);
}

std::unique_ptr<quic::QuicEncryptedPacket> encryptPacket(const quic::QuicReceivedPacket& packet) {
  return std::make_unique<quic::QuicEncryptedPacket>(packet.data(), packet.length());
}

std::unique_ptr<quic::QuicEncryptedPacket>
testEncryptedPacket(quic::ParsedQuicVersion quic_version,
                    const quic::QuicConnectionId& connection_id, const std::string& data) {
  quic::ParsedQuicVersionVector versions(quic::test::SupportedVersions(quic_version));
  return std::unique_ptr<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
      connection_id, quic::EmptyQuicConnectionId(), true, false, 1, data, true,
      quic::CONNECTION_ID_PRESENT, quic::CONNECTION_ID_ABSENT, quic::PACKET_4BYTE_PACKET_NUMBER,
      &versions));
}

std::unique_ptr<quic::QuicReceivedPacket>
wrapPacket(const quic::QuicEncryptedPacket& encrypted_packet, EnvoyQuicClock& clock) {
  return std::unique_ptr<quic::QuicReceivedPacket>(
      quic::test::ConstructReceivedPacket(encrypted_packet, clock.Now()));
}

TEST_P(EnvoyQuicDispatcherTest, ProcessPacketReturnsFalseForNonDispatchedPacket) {
  EnvoyQuicClock clock(*dispatcher_);
  quic::QuicSocketAddress self_addr = envoyIpAddressToQuicSocketAddress(
      listen_socket_->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  // Stop accepting new connections before the CHLO.
  envoy_quic_dispatcher_.StopAcceptingNewConnections();
  // Both the CHLO and the subsequent packet for the same connection should be treated
  // as not dispatched.
  auto chlo_packet = wrapPacket(*encryptPacket(*quic::test::GetFirstFlightOfPackets(
                                    quic_version_, quic_config_, connection_id_)[0]),
                                clock);
  EXPECT_FALSE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *chlo_packet));
  auto packet = wrapPacket(*testEncryptedPacket(quic_version_, connection_id_, "hello"), clock);
  EXPECT_FALSE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *packet));
}

TEST_P(EnvoyQuicDispatcherTest, ProcessPacketReturnsTrueForDispatchedPacket) {
  EnvoyQuicClock clock(*dispatcher_);
  PreparedFilterChainMocks mocks(*this);
  quic::QuicSocketAddress self_addr = envoyIpAddressToQuicSocketAddress(
      listen_socket_->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  auto chlo_packet = wrapPacket(*encryptPacket(*quic::test::GetFirstFlightOfPackets(
                                    quic_version_, quic_config_, connection_id_)[0]),
                                clock);
  EXPECT_TRUE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *chlo_packet));
  auto packet = wrapPacket(*testEncryptedPacket(quic_version_, connection_id_, "hello"), clock);
  EXPECT_TRUE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *packet));
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
  envoy_quic_dispatcher_.Shutdown();
}

TEST_P(EnvoyQuicDispatcherTest, ProcessPacketReturnsTrueForDispatchedPacketOnExistingConnection) {
  EnvoyQuicClock clock(*dispatcher_);
  PreparedFilterChainMocks mocks(*this);
  quic::QuicSocketAddress self_addr = envoyIpAddressToQuicSocketAddress(
      listen_socket_->connectionInfoProvider().localAddress()->ip());
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  auto chlo_packet = wrapPacket(*encryptPacket(*quic::test::GetFirstFlightOfPackets(
                                    quic_version_, quic_config_, connection_id_)[0]),
                                clock);
  EXPECT_TRUE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *chlo_packet));
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);
  // Stop accepting new connections after processing the CHLO.
  envoy_quic_dispatcher_.StopAcceptingNewConnections();
  // Another packet for the existing connection should still be dispatched successfully.
  auto packet = wrapPacket(*testEncryptedPacket(quic_version_, connection_id_, "hello"), clock);
  EXPECT_TRUE(envoy_quic_dispatcher_.processPacket(self_addr, peer_addr, *packet));
  envoy_quic_dispatcher_.Shutdown();
}

TEST_P(EnvoyQuicDispatcherTest, CloseWithGivenFilterChain) {
  Network::MockFilterChainManager filter_chain_manager;
  std::shared_ptr<Network::MockReadFilter> read_filter(new Network::MockReadFilter());
  Network::MockConnectionCallbacks network_connection_callbacks;
  testing::StrictMock<Stats::MockCounter> read_total;
  testing::StrictMock<Stats::MockGauge> read_current;
  testing::StrictMock<Stats::MockCounter> write_total;
  testing::StrictMock<Stats::MockGauge> write_current;

  Filter::NetworkFilterFactoriesList filter_factory;
  filter_factory.push_back(
      std::make_unique<Config::TestExtensionConfigProvider<Network::FilterFactoryCb>>(
          [&](Network::FilterManager& filter_manager) {
            filter_manager.addReadFilter(read_filter);
            read_filter->callbacks_->connection().addConnectionCallbacks(
                network_connection_callbacks);
            read_filter->callbacks_->connection().setConnectionStats(
                {read_total, read_current, write_total, write_current, nullptr, nullptr});
          }));

  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_, _))
      .WillOnce(Return(&proof_source_->filterChain()));
  EXPECT_CALL(proof_source_->filterChain(), networkFilterFactories())
      .WillOnce(ReturnRef(filter_factory));
  EXPECT_CALL(listener_config_, filterChainFactory()).Times(2u);
  EXPECT_CALL(listener_config_.filter_chain_factory_, createQuicListenerFilterChain(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
      .WillOnce(Invoke([](Network::Connection& connection,
                          const Filter::NetworkFilterFactoriesList& filter_factories) {
        EXPECT_EQ(1u, filter_factories.size());
        Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
        return true;
      }));
  EXPECT_CALL(*read_filter, onNewConnection())
      // Stop iteration to avoid calling getRead/WriteBuffer().
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  // Set QuicDispatcher::new_sessions_allowed_per_event_loop_ to
  // |kNumSessionsToCreatePerLoopForTests| so that received CHLOs can be
  // processed immediately.
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);

  processValidChloPacket(peer_addr);

  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  envoy_quic_dispatcher_.closeConnectionsWithFilterChain(&proof_source_->filterChain());
}

TEST_P(EnvoyQuicDispatcherTest, EnvoyQuicCryptoServerStreamHelper) {
  const quic::CryptoHandshakeMessage crypto_message;
  const quic::QuicSocketAddress client_address;
  const quic::QuicSocketAddress peer_address;
  const quic::QuicSocketAddress self_address;

  EnvoyQuicCryptoServerStreamHelper helper;
  EXPECT_ENVOY_BUG(helper.CanAcceptClientHello(crypto_message, client_address, peer_address,
                                               self_address, nullptr),
                   "Unexpected call to CanAcceptClientHello");
}

} // namespace Quic
} // namespace Envoy
