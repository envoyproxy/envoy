#include <openssl/evp.h>

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/platform/api/quic_text_utils.h"
#pragma GCC diagnostic pop

#include <memory>

#include "extensions/quic_listeners/quiche/envoy_quic_connection_helper.h"
#include "common/network/listen_socket_impl.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/environment.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"
#include "test/test_common/network_utility.h"
#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/envoy_quic_dispatcher.h"
#include "extensions/quic_listeners/quiche/envoy_quic_fake_proof_source.h"
#include "extensions/quic_listeners/quiche/envoy_quic_alarm_factory.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/transport_sockets/well_known_names.h"
#include "server/configuration_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace quic {
namespace test {
class QuicDispatcherPeer {
public:
  static quic::QuicTimeWaitListManager* time_wait_list_manager(QuicDispatcher* dispatcher) {
    return dispatcher->time_wait_list_manager_.get();
  }
};

} // namespace test
} // namespace quic

namespace Envoy {
namespace Quic {

class EnvoyQuicDispatcherTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                protected Logger::Loggable<Logger::Id::main> {
public:
  EnvoyQuicDispatcherTest()
      : version_(GetParam()), api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher()), connection_helper_(*dispatcher_),
        crypto_config_(quic::QuicCryptoServerConfig::TESTING, quic::QuicRandom::GetInstance(),
                       std::make_unique<EnvoyQuicFakeProofSource>(),
                       quic::KeyExchangeSource::Default()),
        version_manager_(quic::CurrentSupportedVersions()),
        listener_stats_({ALL_LISTENER_STATS(POOL_COUNTER(listener_config_.listenerScope()),
                                            POOL_GAUGE(listener_config_.listenerScope()),
                                            POOL_HISTOGRAM(listener_config_.listenerScope()))}),
        connection_handler_(ENVOY_LOGGER(), *dispatcher_),
        envoy_quic_dispatcher_(
            &crypto_config_, quic_config_, &version_manager_,
            std::make_unique<EnvoyQuicConnectionHelper>(*dispatcher_),
            std::make_unique<EnvoyQuicAlarmFactory>(*dispatcher_, *connection_helper_.GetClock()),
            quic::kQuicDefaultConnectionIdLength, connection_handler_, listener_config_,
            listener_stats_, *dispatcher_) {
    auto writer = new testing::NiceMock<quic::test::MockPacketWriter>();
    envoy_quic_dispatcher_.InitializeWithWriter(writer);
    EXPECT_CALL(*writer, WritePacket(_, _, _, _, _))
        .WillRepeatedly(Return(quic::WriteResult(quic::WRITE_STATUS_OK, 0)));
  }

  void SetUp() override {
    listen_socket_ = std::make_unique<Network::NetworkListenSocket<
        Network::NetworkSocketTrait<Network::Address::SocketType::Datagram>>>(
        Network::Test::getCanonicalLoopbackAddress(version_), nullptr, /*bind*/ true);
    // Advance time a bit because QuicTime regards 0 as uninitialized timestamp.
    time_system_.sleep(std::chrono::milliseconds(100));
    EXPECT_CALL(listener_config_, socket()).WillRepeatedly(ReturnRef(*listen_socket_));
  }

  void TearDown() override {
    envoy_quic_dispatcher_.Shutdown();
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  std::unique_ptr<quic::QuicReceivedPacket>
  createFullChloPacket(const quic::QuicConnectionId& connection_id,
                       quic::QuicSocketAddress client_address) {
    EnvoyQuicClock clock(*dispatcher_);
    quic::CryptoHandshakeMessage chlo = quic::test::crypto_test_utils::GenerateDefaultInchoateCHLO(
        &clock, quic::AllSupportedVersions()[0].transport_version, &crypto_config_);
    chlo.SetVector(quic::kCOPT, quic::QuicTagVector{quic::kREJ});
    chlo.SetStringPiece(quic::kSNI, "www.abc.com");
    quic::CryptoHandshakeMessage full_chlo;
    quic::QuicReferenceCountedPointer<quic::QuicSignedServerConfig> signed_config(
        new quic::QuicSignedServerConfig);
    quic::QuicCompressedCertsCache cache(
        quic::QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);
    quic::test::crypto_test_utils::GenerateFullCHLO(
        chlo, &crypto_config_,
        envoyAddressInstanceToQuicSocketAddress(listen_socket_->localAddress()), client_address,
        quic::AllSupportedVersions()[0].transport_version, &clock, signed_config, &cache,
        &full_chlo);
    // Overwrite version label to highest current supported version.
    full_chlo.SetVersion(quic::kVER, quic::CurrentSupportedVersions()[0]);
    quic::QuicConfig quic_config;
    quic_config.ToHandshakeMessage(&full_chlo,
                                   quic::CurrentSupportedVersions()[0].transport_version);

    std::string packet_content(full_chlo.GetSerialized().AsStringPiece());
    std::unique_ptr<quic::QuicEncryptedPacket> encrypted_packet(
        quic::test::ConstructEncryptedPacket(connection_id, quic::EmptyQuicConnectionId(),
                                             /*version_flag=*/true, /*reset_flag*/ false,
                                             /*packet_number=*/1, packet_content));
    return std::unique_ptr<quic::QuicReceivedPacket>(
        quic::test::ConstructReceivedPacket(*encrypted_packet, clock.Now()));
  }

protected:
  Network::Address::IpVersion version_;
  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::SocketPtr listen_socket_;
  EnvoyQuicConnectionHelper connection_helper_;
  quic::QuicCryptoServerConfig crypto_config_;
  quic::QuicConfig quic_config_;
  quic::QuicVersionManager version_manager_;

  testing::NiceMock<Network::MockListenerConfig> listener_config_;
  Server::ListenerStats listener_stats_;
  Server::ConnectionHandlerImpl connection_handler_;
  EnvoyQuicDispatcher envoy_quic_dispatcher_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, EnvoyQuicDispatcherTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(EnvoyQuicDispatcherTest, CreateNewConnectionUponCHLO) {
  quic::SetVerbosityLogThreshold(2);
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  Network::MockFilterChain filter_chain;
  Network::MockFilterChainManager filter_chain_manager;
  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_))
      .WillOnce(Invoke([&](const Network::ConnectionSocket& socket) {
        EXPECT_EQ(*listen_socket_->localAddress(), *socket.localAddress());
        EXPECT_EQ(Extensions::TransportSockets::TransportSocketNames::get().Quic,
                  socket.detectedTransportProtocol());
        EXPECT_EQ(peer_addr, envoyAddressInstanceToQuicSocketAddress(socket.remoteAddress()));
        return &filter_chain;
      }));
  std::shared_ptr<Network::MockReadFilter> read_filter(new Network::MockReadFilter());
  Network::MockConnectionCallbacks network_connection_callbacks;
  std::vector<Network::FilterFactoryCb> filter_factory(
      {[&](Network::FilterManager& filter_manager) {
        filter_manager.addReadFilter(read_filter);
        read_filter->callbacks_->connection().addConnectionCallbacks(network_connection_callbacks);
      }});
  EXPECT_CALL(filter_chain, networkFilterFactories()).WillOnce(ReturnRef(filter_factory));
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
      .WillOnce(Invoke([]() { return Network::FilterStatus::StopIteration; }));

  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  // Upon receiving a full CHLO. A new quic connection should be created and have its filter
  // installed based on self and peer address.
  std::unique_ptr<quic::QuicReceivedPacket> received_packet =
      createFullChloPacket(connection_id, peer_addr);
  envoy_quic_dispatcher_.ProcessPacket(
      envoyAddressInstanceToQuicSocketAddress(listen_socket_->localAddress()), peer_addr,
      *received_packet);
  EXPECT_EQ(1u, envoy_quic_dispatcher_.session_map().size());
  EXPECT_TRUE(
      envoy_quic_dispatcher_.session_map().find(connection_id)->second->IsEncryptionEstablished());
  EXPECT_EQ(1u, connection_handler_.numConnections());
  EXPECT_EQ("www.abc.com", read_filter->callbacks_->connection().requestedServerName());
  EXPECT_EQ(peer_addr, envoyAddressInstanceToQuicSocketAddress(
                           read_filter->callbacks_->connection().remoteAddress()));
  EXPECT_EQ(*listen_socket_->localAddress(), *read_filter->callbacks_->connection().localAddress());
  EXPECT_CALL(network_connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  // Shutdown() to close the connection.
  envoy_quic_dispatcher_.Shutdown();
}

TEST_P(EnvoyQuicDispatcherTest, CloseConnectionDueToMissingFilterChain) {
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  Network::MockFilterChainManager filter_chain_manager;
  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_))
      .WillOnce(Invoke([&](const Network::ConnectionSocket& socket) {
        EXPECT_EQ(*listen_socket_->localAddress(), *socket.localAddress());
        EXPECT_EQ(peer_addr, envoyAddressInstanceToQuicSocketAddress(socket.remoteAddress()));
        return nullptr;
      }));
  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  std::unique_ptr<quic::QuicReceivedPacket> received_packet =
      createFullChloPacket(connection_id, peer_addr);
  envoy_quic_dispatcher_.ProcessPacket(
      envoyAddressInstanceToQuicSocketAddress(listen_socket_->localAddress()), peer_addr,
      *received_packet);
  EXPECT_EQ(0u, envoy_quic_dispatcher_.session_map().size());
  EXPECT_EQ(0u, connection_handler_.numConnections());
  EXPECT_TRUE(quic::test::QuicDispatcherPeer::time_wait_list_manager(&envoy_quic_dispatcher_)
                  ->IsConnectionIdInTimeWait(connection_id));
  EXPECT_EQ(1u, listener_stats_.no_filter_chain_match_.value());
}

TEST_P(EnvoyQuicDispatcherTest, CloseConnectionDueToEmptyFilterChain) {
  quic::QuicSocketAddress peer_addr(version_ == Network::Address::IpVersion::v4
                                        ? quic::QuicIpAddress::Loopback4()
                                        : quic::QuicIpAddress::Loopback6(),
                                    54321);
  Network::MockFilterChain filter_chain;
  Network::MockFilterChainManager filter_chain_manager;
  EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager));
  EXPECT_CALL(filter_chain_manager, findFilterChain(_))
      .WillOnce(Invoke([&](const Network::ConnectionSocket& socket) {
        EXPECT_EQ(*listen_socket_->localAddress(), *socket.localAddress());
        EXPECT_EQ(peer_addr, envoyAddressInstanceToQuicSocketAddress(socket.remoteAddress()));
        return &filter_chain;
      }));
  // Empty filter_factory should cause connection close.
  std::vector<Network::FilterFactoryCb> filter_factory;
  EXPECT_CALL(filter_chain, networkFilterFactories()).WillOnce(ReturnRef(filter_factory));

  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  std::unique_ptr<quic::QuicReceivedPacket> received_packet =
      createFullChloPacket(connection_id, peer_addr);
  envoy_quic_dispatcher_.ProcessPacket(
      envoyAddressInstanceToQuicSocketAddress(listen_socket_->localAddress()), peer_addr,
      *received_packet);
  EXPECT_EQ(0u, envoy_quic_dispatcher_.session_map().size());
  EXPECT_EQ(0u, connection_handler_.numConnections());
  EXPECT_TRUE(quic::test::QuicDispatcherPeer::time_wait_list_manager(&envoy_quic_dispatcher_)
                  ->IsConnectionIdInTimeWait(connection_id));
}

} // namespace Quic
} // namespace Envoy
