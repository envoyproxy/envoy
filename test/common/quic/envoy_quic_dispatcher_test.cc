#include <openssl/evp.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/test_tools/quic_dispatcher_peer.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <memory>

#include "source/common/quic/envoy_quic_connection_helper.h"
#include "source/common/network/listen_socket_impl.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/environment.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"
#include "test/test_common/network_utility.h"
#include "source/common/quic/platform/envoy_quic_clock.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/envoy_quic_dispatcher.h"
#include "source/common/quic/envoy_quic_server_session.h"
#include "test/common/quic/test_proof_source.h"
#include "test/common/quic/test_utils.h"
#include "source/common/quic/envoy_quic_alarm_factory.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/server/configuration_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

namespace {
const size_t kNumSessionsToCreatePerLoopForTests = 16;
}

class EnvoyQuicDispatcherTest : public QuicMultiVersionTest,
                                protected Logger::Loggable<Logger::Id::main> {
public:
  EnvoyQuicDispatcherTest()
      : version_(GetParam().first), api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        listen_socket_(std::make_unique<Network::NetworkListenSocket<
                           Network::NetworkSocketTrait<Network::Socket::Type::Datagram>>>(
            Network::Test::getCanonicalLoopbackAddress(version_), nullptr, /*bind*/ true)),
        connection_helper_(*dispatcher_), proof_source_(new TestProofSource()),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::unique_ptr<TestProofSource>(proof_source_),
                       quic::KeyExchangeSource::Default()),
        version_manager_([]() {
          if (GetParam().second == QuicVersionType::GquicQuicCrypto) {
            return quic::CurrentSupportedVersionsWithQuicCrypto();
          }
          bool use_http3 = GetParam().second == QuicVersionType::Iquic;
          SetQuicReloadableFlag(quic_disable_version_draft_29, !use_http3);
          SetQuicReloadableFlag(quic_disable_version_rfcv1, !use_http3);
          return quic::CurrentSupportedVersions();
        }()),
        quic_version_(version_manager_.GetSupportedVersions()[0]),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
            POOL_COUNTER_PREFIX(listener_config_.listenerScope(), "worker."),
            POOL_GAUGE_PREFIX(listener_config_.listenerScope(), "worker."))}),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()),
        connection_handler_(*dispatcher_, absl::nullopt),
        envoy_quic_dispatcher_(
            &crypto_config_, quic_config_, &version_manager_,
            std::make_unique<EnvoyQuicConnectionHelper>(*dispatcher_),
            std::make_unique<EnvoyQuicAlarmFactory>(*dispatcher_, *connection_helper_.GetClock()),
            quic::kQuicDefaultConnectionIdLength, connection_handler_, listener_config_,
            listener_stats_, per_worker_stats_, *dispatcher_, *listen_socket_, quic_stat_names_,
            crypto_stream_factory_),
        connection_id_(quic::test::TestConnectionId(1)) {
    auto writer = new testing::NiceMock<quic::test::MockPacketWriter>();
    envoy_quic_dispatcher_.InitializeWithWriter(writer);
    EXPECT_CALL(*writer, WritePacket(_, _, _, _, _))
        .WillRepeatedly(Return(quic::WriteResult(quic::WRITE_STATUS_OK, 0)));
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
    Buffer::OwnedImpl payload = generateChloPacketToSend(
        quic_version_, quic_config_, crypto_config_, connection_id_, clock,
        envoyIpAddressToQuicSocketAddress(listen_socket_->addressProvider().localAddress()->ip()),
        peer_addr, "test.example.org");
    Buffer::RawSliceVector slice = payload.getRawSlices();
    ASSERT(slice.size() == 1);
    auto encrypted_packet = std::make_unique<quic::QuicEncryptedPacket>(
        static_cast<char*>(slice[0].mem_), slice[0].len_);
    std::unique_ptr<quic::QuicReceivedPacket> received_packet =
        std::unique_ptr<quic::QuicReceivedPacket>(
            quic::test::ConstructReceivedPacket(*encrypted_packet, clock.Now()));

    envoy_quic_dispatcher_.ProcessPacket(
        envoyIpAddressToQuicSocketAddress(listen_socket_->addressProvider().localAddress()->ip()),
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
    EXPECT_EQ("test.example.org", envoy_connection->requestedServerName());
    EXPECT_EQ(peer_addr, envoyIpAddressToQuicSocketAddress(
                             envoy_connection->addressProvider().remoteAddress()->ip()));
    ASSERT(envoy_connection->addressProvider().localAddress() != nullptr);
    EXPECT_EQ(*listen_socket_->addressProvider().localAddress(),
              *envoy_connection->addressProvider().localAddress());
    EXPECT_EQ(64 * 1024, envoy_connection->max_inbound_header_list_size());
  }

  void processValidChloPacketAndInitializeFilters(bool should_buffer) {
    Network::MockFilterChainManager filter_chain_manager;
    std::shared_ptr<Network::MockReadFilter> read_filter(new Network::MockReadFilter());
    Network::MockConnectionCallbacks network_connection_callbacks;
    testing::StrictMock<Stats::MockCounter> read_total;
    testing::StrictMock<Stats::MockGauge> read_current;
    testing::StrictMock<Stats::MockCounter> write_total;
    testing::StrictMock<Stats::MockGauge> write_current;

    std::vector<Network::FilterFactoryCb> filter_factory(
        {[&](Network::FilterManager& filter_manager) {
          filter_manager.addReadFilter(read_filter);
          read_filter->callbacks_->connection().addConnectionCallbacks(
              network_connection_callbacks);
          read_filter->callbacks_->connection().setConnectionStats(
              {read_total, read_current, write_total, write_current, nullptr, nullptr});
        }});
    EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
    EXPECT_CALL(filter_chain_manager, findFilterChain(_))
        .WillOnce(Invoke([this](const Network::ConnectionSocket& socket) {
          switch (GetParam().second) {
          case QuicVersionType::GquicQuicCrypto:
            EXPECT_EQ("", socket.requestedApplicationProtocols()[0]);
            break;
          case QuicVersionType::GquicTls:
            EXPECT_EQ("h3-T051", socket.requestedApplicationProtocols()[0]);
            break;
          case QuicVersionType::Iquic:
            EXPECT_EQ("h3", socket.requestedApplicationProtocols()[0]);
            break;
          }
          EXPECT_EQ("test.example.org", socket.requestedServerName());
          return &proof_source_->filterChain();
        }));
    Network::MockTransportSocketFactory transport_socket_factory;
    EXPECT_CALL(proof_source_->filterChain(), transportSocketFactory())
        .WillOnce(ReturnRef(transport_socket_factory));
    EXPECT_CALL(proof_source_->filterChain(), networkFilterFactories())
        .WillOnce(ReturnRef(filter_factory));
    EXPECT_CALL(listener_config_, filterChainFactory());
    EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
        .WillOnce(Invoke([](Network::Connection& connection,
                            const std::vector<Network::FilterFactoryCb>& filter_factories) {
          EXPECT_EQ(1u, filter_factories.size());
          Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
          dynamic_cast<EnvoyQuicServerSession&>(connection)
              .set_max_inbound_header_list_size(64 * 1024);
          return true;
        }));
    EXPECT_CALL(*read_filter, onNewConnection())
        // Stop iteration to avoid calling getRead/WriteBuffer().
        .WillOnce(Return(Network::FilterStatus::StopIteration));
    if (!quicVersionUsesTls()) {
      // The test utility can't generate 0-RTT packet for Quic TLS handshake yet.
      EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
    }

    processValidChloPacketAndCheckStatus(should_buffer);
    EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
    // Shutdown() to close the connection.
    envoy_quic_dispatcher_.Shutdown();
  }

  bool quicVersionUsesTls() { return quic_version_.UsesTls(); }

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
  EnvoyQuicDispatcher envoy_quic_dispatcher_;
  const quic::QuicConnectionId connection_id_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyQuicDispatcherTests, EnvoyQuicDispatcherTest,
                         testing::ValuesIn(generateTestParam()), testParamsToString);

TEST_P(EnvoyQuicDispatcherTest, CreateNewConnectionUponCHLO) {
  processValidChloPacketAndInitializeFilters(false);
}

TEST_P(EnvoyQuicDispatcherTest, CloseConnectionDuringFilterInstallation) {
  Network::MockFilterChainManager filter_chain_manager;
  std::shared_ptr<Network::MockReadFilter> read_filter(new Network::MockReadFilter());
  Network::MockConnectionCallbacks network_connection_callbacks;
  testing::StrictMock<Stats::MockCounter> read_total;
  testing::StrictMock<Stats::MockGauge> read_current;
  testing::StrictMock<Stats::MockCounter> write_total;
  testing::StrictMock<Stats::MockGauge> write_current;

  std::vector<Network::FilterFactoryCb> filter_factory(
      {[&](Network::FilterManager& filter_manager) {
        filter_manager.addReadFilter(read_filter);
        read_filter->callbacks_->connection().addConnectionCallbacks(network_connection_callbacks);
        read_filter->callbacks_->connection().setConnectionStats(
            {read_total, read_current, write_total, write_current, nullptr, nullptr});
        // This will not close connection right away, but after it processes the first packet.
        read_filter->callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
      }});
  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_))
      .WillOnce(Return(&proof_source_->filterChain()));
  Network::MockTransportSocketFactory transport_socket_factory;
  EXPECT_CALL(proof_source_->filterChain(), transportSocketFactory())
      .WillOnce(ReturnRef(transport_socket_factory));
  EXPECT_CALL(proof_source_->filterChain(), networkFilterFactories())
      .WillOnce(ReturnRef(filter_factory));
  EXPECT_CALL(listener_config_, filterChainFactory());
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
      .WillOnce(Invoke([](Network::Connection& connection,
                          const std::vector<Network::FilterFactoryCb>& filter_factories) {
        EXPECT_EQ(1u, filter_factories.size());
        Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
        return true;
      }));
  EXPECT_CALL(*read_filter, onNewConnection())
      // Stop iteration to avoid calling getRead/WriteBuffer().
      .WillOnce(Return(Network::FilterStatus::StopIteration));

  if (!quicVersionUsesTls()) {
    EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  }

  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  // Set QuicDispatcher::new_sessions_allowed_per_event_loop_ to
  // |kNumSessionsToCreatePerLoopForTests| so that received CHLOs can be
  // processed immediately.
  envoy_quic_dispatcher_.ProcessBufferedChlos(kNumSessionsToCreatePerLoopForTests);

  processValidChloPacket(peer_addr);
}

TEST_P(EnvoyQuicDispatcherTest, CreateNewConnectionUponBufferedCHLO) {
  processValidChloPacketAndInitializeFilters(true);
}

} // namespace Quic
} // namespace Envoy
