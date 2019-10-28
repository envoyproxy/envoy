#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

#pragma GCC diagnostic pop

#include "server/configuration_impl.h"
#include "common/common/logger.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_factory.h"
#include "extensions/quic_listeners/quiche/active_quic_listener.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/environment.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"
#include "test/test_common/network_utility.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class ActiveQuicListenerPeer {
public:
  static EnvoyQuicDispatcher* quic_dispatcher(ActiveQuicListener& listener) {
    return listener.quic_dispatcher_.get();
  }

  static quic::QuicCryptoServerConfig& crypto_config(ActiveQuicListener& listener) {
    return *listener.crypto_config_;
  }
};

class ActiveQuicListenerTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ActiveQuicListenerTest()
      : version_(GetParam()), api_(Api::createApiForTest(simulated_time_system_)),
        dispatcher_(api_->allocateDispatcher()), read_filter_(new Network::MockReadFilter()),
        filter_factory_({[this](Network::FilterManager& filter_manager) {
          filter_manager.addReadFilter(read_filter_);
          read_filter_->callbacks_->connection().addConnectionCallbacks(
              network_connection_callbacks_);
        }}),
        connection_handler_(*dispatcher_, "test_thread") {}

  void SetUp() override {
    listen_socket_ = std::make_unique<Network::NetworkListenSocket<
        Network::NetworkSocketTrait<Network::Address::SocketType::Datagram>>>(
        Network::Test::getCanonicalLoopbackAddress(version_), nullptr, /*bind*/ true);
    listen_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    listen_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    client_socket_ = std::make_unique<Network::NetworkListenSocket<
        Network::NetworkSocketTrait<Network::Address::SocketType::Datagram>>>(
        Network::Test::getCanonicalLoopbackAddress(version_), nullptr, /*bind*/ false);
    EXPECT_CALL(listener_config_, socket()).WillRepeatedly(ReturnRef(*listen_socket_));
    ON_CALL(listener_config_, filterChainManager()).WillByDefault(ReturnRef(filter_chain_manager_));
    ON_CALL(filter_chain_manager_, findFilterChain(_)).WillByDefault(Return(&filter_chain_));
    ON_CALL(filter_chain_, networkFilterFactories()).WillByDefault(ReturnRef(filter_factory_));
    ON_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
        .WillByDefault(Invoke([](Network::Connection& connection,
                                 const std::vector<Network::FilterFactoryCb>& filter_factories) {
          EXPECT_EQ(1u, filter_factories.size());
          Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
          return true;
        }));

    quic_listener_ = std::make_unique<ActiveQuicListener>(*dispatcher_, connection_handler_,
                                                          listener_config_, quic_config_);
    simulated_time_system_.sleep(std::chrono::milliseconds(100));
  }

  void TearDown() override {
    quic_listener_->onListenerShutdown();
    // Trigger alarm to fire before listener destruction.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

protected:
  Network::Address::IpVersion version_;
  Event::SimulatedTimeSystemHelper simulated_time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::SocketPtr listen_socket_;
  Network::SocketPtr client_socket_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  std::vector<Network::FilterFactoryCb> filter_factory_;
  Network::MockFilterChain filter_chain_;
  Network::MockFilterChainManager filter_chain_manager_;
  NiceMock<Network::MockListenerConfig> listener_config_;
  quic::QuicConfig quic_config_;
  Server::ConnectionHandlerImpl connection_handler_;
  std::unique_ptr<ActiveQuicListener> quic_listener_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ActiveQuicListenerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ActiveQuicListenerTest, ReceiveFullQuicCHLO) {
  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  EnvoyQuicClock clock(*dispatcher_);
  quic::CryptoHandshakeMessage chlo = quic::test::crypto_test_utils::GenerateDefaultInchoateCHLO(
      &clock, quic::AllSupportedVersions()[0].transport_version,
      &ActiveQuicListenerPeer::crypto_config(*quic_listener_));
  chlo.SetVector(quic::kCOPT, quic::QuicTagVector{quic::kREJ});
  quic::CryptoHandshakeMessage full_chlo;
  quic::QuicReferenceCountedPointer<quic::QuicSignedServerConfig> signed_config(
      new quic::QuicSignedServerConfig);
  quic::QuicCompressedCertsCache cache(
      quic::QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);
  quic::test::crypto_test_utils::GenerateFullCHLO(
      chlo, &ActiveQuicListenerPeer::crypto_config(*quic_listener_),
      envoyAddressInstanceToQuicSocketAddress(listen_socket_->localAddress()),
      envoyAddressInstanceToQuicSocketAddress(client_socket_->localAddress()),
      quic::AllSupportedVersions()[0].transport_version, &clock, signed_config, &cache, &full_chlo);
  // Overwrite version label to highest current supported version.
  full_chlo.SetVersion(quic::kVER, quic::CurrentSupportedVersions()[0]);
  quic::QuicConfig quic_config;
  quic_config.ToHandshakeMessage(&full_chlo, quic::CurrentSupportedVersions()[0].transport_version);

  std::string packet_content(full_chlo.GetSerialized().AsStringPiece());
  auto encrypted_packet =
      std::unique_ptr<quic::QuicEncryptedPacket>(quic::test::ConstructEncryptedPacket(
          connection_id, quic::EmptyQuicConnectionId(), /*version_flag=*/true, /*reset_flag*/ false,
          /*packet_number=*/1, packet_content));

  Buffer::RawSlice first_slice{reinterpret_cast<void*>(const_cast<char*>(encrypted_packet->data())),
                               encrypted_packet->length()};
  // Send a full CHLO to finish 0-RTT handshake.
  auto send_rc =
      client_socket_->ioHandle().sendto(first_slice, /*flags=*/0, *listen_socket_->localAddress());
  ASSERT_EQ(encrypted_packet->length(), send_rc.rc_);

  EXPECT_CALL(listener_config_, filterChainManager());
  EXPECT_CALL(filter_chain_manager_, findFilterChain(_));
  EXPECT_CALL(filter_chain_, networkFilterFactories());
  EXPECT_CALL(listener_config_, filterChainFactory());
  EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _));
  EXPECT_CALL(*read_filter_, onNewConnection())
      // Stop iteration to avoid calling getRead/WriteBuffer().
      .WillOnce(Invoke([this]() {
        dispatcher_->exit();
        return Network::FilterStatus::StopIteration;
      }));
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::Connected));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  Buffer::InstancePtr result_buffer(new Buffer::OwnedImpl());
  const uint64_t bytes_to_read = 11;
  uint64_t bytes_read = 0;
  int retry = 0;

  do {
    Api::IoCallUint64Result result =
        result_buffer->read(client_socket_->ioHandle(), bytes_to_read - bytes_read);

    if (result.ok()) {
      bytes_read += result.rc_;
    } else if (retry == 10 || result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
      break;
    }

    if (bytes_read == bytes_to_read) {
      break;
    }

    retry++;
    ::usleep(10000);
  } while (true);
  // TearDown() will close the connection.
  EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose));
}

} // namespace Quic
} // namespace Envoy
