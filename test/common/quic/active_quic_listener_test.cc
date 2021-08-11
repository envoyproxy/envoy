#include <cstdlib>
#include <memory>

#include "envoy/config/listener/v3/quic_config.pb.validate.h"
#include "envoy/network/exception.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_dispatcher_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"
#include "quiche/quic/test_tools/quic_crypto_server_config_peer.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "source/server/configuration_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/quic/active_quic_listener.h"
#include "source/common/http/utility.h"
#include "test/common/quic/test_utils.h"
#include "test/common/quic/test_proof_source.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/environment.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"
#include "test/test_common/network_utility.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "source/common/quic/platform/envoy_quic_clock.h"
#include "source/common/quic/envoy_quic_utils.h"
#include "source/common/quic/udp_gso_batch_writer.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class ActiveQuicListenerPeer {
public:
  static EnvoyQuicDispatcher* quicDispatcher(ActiveQuicListener& listener) {
    return listener.quic_dispatcher_.get();
  }

  static quic::QuicCryptoServerConfig& cryptoConfig(ActiveQuicListener& listener) {
    return *listener.crypto_config_;
  }

  static bool enabled(ActiveQuicListener& listener) { return listener.enabled_->enabled(); }
};

class ActiveQuicListenerFactoryPeer {
public:
  static envoy::config::core::v3::RuntimeFeatureFlag&
  runtimeEnabled(ActiveQuicListenerFactory* factory) {
    return factory->enabled_;
  }
};

class ActiveQuicListenerTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ActiveQuicListenerTest()
      : version_(GetParam()), api_(Api::createApiForTest(simulated_time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), clock_(*dispatcher_),
        local_address_(Network::Test::getCanonicalLoopbackAddress(version_)),
        connection_handler_(*dispatcher_, absl::nullopt),
        quic_version_(quic::CurrentSupportedHttp3Versions()[0]),
        quic_stat_names_(listener_config_.listenerScope().symbolTable()) {}

  template <typename A, typename B>
  std::unique_ptr<A> staticUniquePointerCast(std::unique_ptr<B>&& source) {
    return std::unique_ptr<A>{static_cast<A*>(source.release())};
  }

  void SetUp() override {
    envoy::config::bootstrap::v3::LayeredRuntime config;
    config.add_layers()->mutable_admin_layer();
    loader_ = std::make_unique<Runtime::ScopedLoaderSingleton>(
        Runtime::LoaderPtr{new Runtime::LoaderImpl(*dispatcher_, tls_, config, local_info_, store_,
                                                   generator_, validation_visitor_, *api_)});

    listen_socket_ =
        std::make_shared<Network::UdpListenSocket>(local_address_, nullptr, /*bind*/ true);
    listen_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    listen_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    ASSERT_TRUE(Network::Socket::applyOptions(listen_socket_->options(), *listen_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));

    ON_CALL(listener_config_, listenSocketFactory()).WillByDefault(ReturnRef(socket_factory_));
    ON_CALL(socket_factory_, getListenSocket(_)).WillByDefault(Return(listen_socket_));

    // Use UdpGsoBatchWriter to perform non-batched writes for the purpose of this test, if it is
    // supported.
    ON_CALL(listener_config_, udpListenerConfig())
        .WillByDefault(Return(Network::UdpListenerConfigOptRef(udp_listener_config_)));
    ON_CALL(udp_listener_config_, packetWriterFactory())
        .WillByDefault(ReturnRef(udp_packet_writer_factory_));
    ON_CALL(udp_packet_writer_factory_, createUdpPacketWriter(_, _))
        .WillByDefault(Invoke(
            [&](Network::IoHandle& io_handle, Stats::Scope& scope) -> Network::UdpPacketWriterPtr {
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
              return std::make_unique<Quic::UdpGsoBatchWriter>(io_handle, scope);
#else
              UNREFERENCED_PARAMETER(scope);
              return std::make_unique<Network::UdpDefaultWriter>(io_handle);
#endif
            }));
  }

  void initialize() {
    listener_factory_ = createQuicListenerFactory(yamlForQuicConfig());
    EXPECT_CALL(listener_config_, filterChainManager())
        .WillRepeatedly(ReturnRef(filter_chain_manager_));
    quic_listener_ =
        staticUniquePointerCast<ActiveQuicListener>(listener_factory_->createActiveUdpListener(
            0, connection_handler_, *dispatcher_, listener_config_));
    quic_dispatcher_ = ActiveQuicListenerPeer::quicDispatcher(*quic_listener_);
    quic::QuicCryptoServerConfig& crypto_config =
        ActiveQuicListenerPeer::cryptoConfig(*quic_listener_);
    quic::test::QuicCryptoServerConfigPeer crypto_config_peer(&crypto_config);
    auto proof_source = std::make_unique<TestProofSource>();
    filter_chain_ = &proof_source->filterChain();
    crypto_config_peer.ResetProofSource(std::move(proof_source));
    simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(100), *dispatcher_,
                                             Event::Dispatcher::RunType::NonBlock);

    // The state of whether client hellos can be buffered or not is different before and after
    // the first packet processed by the listener. This only matters in tests. Force an event
    // to get it into a consistent state.
    dispatcher_->post([this]() { quic_listener_->onReadReady(); });

    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  Network::ActiveUdpListenerFactoryPtr createQuicListenerFactory(const std::string& yaml) {
    envoy::config::listener::v3::QuicProtocolOptions options;
    TestUtility::loadFromYamlAndValidate(yaml, options);
    return std::make_unique<ActiveQuicListenerFactory>(options, /*concurrency=*/1,
                                                       quic_stat_names_);
  }

  void maybeConfigureMocks(int connection_count) {
    EXPECT_CALL(filter_chain_manager_, findFilterChain(_))
        .Times(connection_count)
        .WillRepeatedly(Return(filter_chain_));
    EXPECT_CALL(listener_config_, filterChainFactory()).Times(connection_count);
    EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
        .Times(connection_count)
        .WillRepeatedly(Invoke([](Network::Connection& connection,
                                  const std::vector<Network::FilterFactoryCb>& filter_factories) {
          EXPECT_EQ(1u, filter_factories.size());
          Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
          return true;
        }));
    if (!quic_version_.UsesTls()) {
      EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
          .Times(connection_count);
    }
    EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::LocalClose))
        .Times(connection_count);

    testing::Sequence seq;
    for (int i = 0; i < connection_count; ++i) {
      auto read_filter = std::make_shared<Network::MockReadFilter>();
      filter_factories_.push_back(
          {Network::FilterFactoryCb([read_filter, this](Network::FilterManager& filter_manager) {
            filter_manager.addReadFilter(read_filter);
            read_filter->callbacks_->connection().addConnectionCallbacks(
                network_connection_callbacks_);
          })});
      // Stop iteration to avoid calling getRead/WriteBuffer().
      EXPECT_CALL(*read_filter, onNewConnection())
          .WillOnce(Return(Network::FilterStatus::StopIteration));
      read_filters_.push_back(std::move(read_filter));
      // A Sequence must be used to allow multiple EXPECT_CALL().WillOnce()
      // calls for the same object.
      EXPECT_CALL(*filter_chain_, transportSocketFactory())
          .InSequence(seq)
          .WillOnce(ReturnRef(transport_socket_factory_));
      EXPECT_CALL(*filter_chain_, networkFilterFactories())
          .InSequence(seq)
          .WillOnce(ReturnRef(filter_factories_.back()));
    }
  }

  void sendCHLO(quic::QuicConnectionId connection_id) {
    client_sockets_.push_back(std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram,
                                                                    local_address_, nullptr));
    Buffer::OwnedImpl payload =
        generateChloPacketToSend(quic_version_, quic_config_, connection_id);
    Buffer::RawSliceVector slice = payload.getRawSlices();
    ASSERT_EQ(1u, slice.size());
    // Send a full CHLO to finish 0-RTT handshake.
    auto send_rc =
        Network::Utility::writeToSocket(client_sockets_.back()->ioHandle(), slice.data(), 1,
                                        nullptr, *listen_socket_->addressProvider().localAddress());
    ASSERT_EQ(slice[0].len_, send_rc.return_value_);

#if defined(__APPLE__)
    // This sleep makes the tests pass more reliably. Some debugging showed that without this,
    // no packet is received when the event loop is running.
    // TODO(ggreenway): make tests more reliable, and handle packet loss during the tests, possibly
    // by retransmitting on a timer.
    ::usleep(1000);
#endif
  }

  void readFromClientSockets() {
    for (auto& client_socket : client_sockets_) {
      Buffer::InstancePtr result_buffer(new Buffer::OwnedImpl());
      const uint64_t bytes_to_read = 11;
      uint64_t bytes_read = 0;
      int retry = 0;

      do {
        Api::IoCallUint64Result result =
            client_socket->ioHandle().read(*result_buffer, bytes_to_read - bytes_read);

        if (result.ok()) {
          bytes_read += result.return_value_;
        } else if (retry == 10 || result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
          break;
        }

        if (bytes_read == bytes_to_read) {
          break;
        }

        retry++;
        absl::SleepFor(absl::Milliseconds(10));
      } while (true);
    }
  }

  void TearDown() override {
    if (quic_listener_ != nullptr) {
      quic_listener_->onListenerShutdown();
    }
    // Trigger alarm to fire before listener destruction.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    Runtime::LoaderSingleton::clear();
  }

protected:
  virtual std::string yamlForQuicConfig() {
    return fmt::format(R"EOF(
    quic_protocol_options:
      initial_connection_window_size: {}
      initial_stream_window_size: {}
    enabled:
      default_value: true
      runtime_key: quic.enabled
    packets_to_read_to_connection_count_ratio: 50
    crypto_stream_config:
      name: "envoy.quic.crypto_stream.server.quiche"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.quic.crypto_stream.v3.CryptoServerStreamConfig
    proof_source_config:
      name: "envoy.quic.proof_source.filter_chain"
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.quic.proof_source.v3.ProofSourceConfig
)EOF",
                       connection_window_size_, stream_window_size_);
  }

  Network::Address::IpVersion version_;
  Event::SimulatedTimeSystemHelper simulated_time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EnvoyQuicClock clock_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::SocketSharedPtr listen_socket_;
  Network::SocketPtr client_socket_;
  std::shared_ptr<Network::MockReadFilter> read_filter_;
  Network::MockConnectionCallbacks network_connection_callbacks_;
  NiceMock<Network::MockUdpListenerConfig> udp_listener_config_;
  NiceMock<Network::MockListenerConfig> listener_config_;
  NiceMock<Network::MockUdpPacketWriterFactory> udp_packet_writer_factory_;
  quic::QuicConfig quic_config_;
  Server::ConnectionHandlerImpl connection_handler_;
  std::unique_ptr<ActiveQuicListener> quic_listener_;
  Network::ActiveUdpListenerFactoryPtr listener_factory_;
  NiceMock<Network::MockListenSocketFactory> socket_factory_;
  EnvoyQuicDispatcher* quic_dispatcher_;
  std::unique_ptr<Runtime::ScopedLoaderSingleton> loader_;

  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::TestUtil::TestStore store_;
  Random::MockRandomGenerator generator_;
  Random::MockRandomGenerator rand_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Init::MockManager init_manager_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  std::list<std::unique_ptr<Network::SocketImpl>> client_sockets_;
  std::list<std::shared_ptr<Network::MockReadFilter>> read_filters_;
  Network::MockFilterChainManager filter_chain_manager_;
  // The following two containers must guarantee pointer stability as addresses
  // of elements are saved in expectations before new elements are added.
  std::list<std::vector<Network::FilterFactoryCb>> filter_factories_;
  const Network::MockFilterChain* filter_chain_;
  Network::MockTransportSocketFactory transport_socket_factory_;
  quic::ParsedQuicVersion quic_version_;
  uint32_t connection_window_size_{1024u};
  uint32_t stream_window_size_{1024u};
  QuicStatNames quic_stat_names_;
};

INSTANTIATE_TEST_SUITE_P(ActiveQuicListenerTests, ActiveQuicListenerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ActiveQuicListenerTest, ReceiveCHLO) {
  initialize();
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  maybeConfigureMocks(/* connection_count = */ 1);
  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  sendCHLO(connection_id);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(buffered_packets->HasChlosBuffered());
  EXPECT_NE(0u, quic_dispatcher_->NumSessions());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_per_event_loop_read_limit")) {
    EXPECT_EQ(50 * quic_dispatcher_->NumSessions(),
              quic_listener_->numPacketsExpectedPerEventLoop());
  }
  const quic::QuicSession* session =
      quic::test::QuicDispatcherPeer::FindSession(quic_dispatcher_, connection_id);
  ASSERT(session != nullptr);
  // 1024 is too small for QUICHE, should be adjusted to the minimum supported by QUICHE.
  EXPECT_EQ(quic::kMinimumFlowControlSendWindow, const_cast<quic::QuicSession*>(session)
                                                     ->config()
                                                     ->GetInitialSessionFlowControlWindowToSend());
  EXPECT_EQ(stream_window_size_, const_cast<quic::QuicSession*>(session)
                                     ->config()
                                     ->GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  readFromClientSockets();
}

TEST_P(ActiveQuicListenerTest, ConfigureReasonableInitialFlowControlWindow) {
  // These initial flow control windows should be accepted by QUIC.
  connection_window_size_ = 64 * 1024;
  stream_window_size_ = 32 * 1024;
  initialize();
  maybeConfigureMocks(/* connection_count = */ 1);
  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  sendCHLO(connection_id);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  const quic::QuicSession* session =
      quic::test::QuicDispatcherPeer::FindSession(quic_dispatcher_, connection_id);
  ASSERT(session != nullptr);
  EXPECT_EQ(connection_window_size_, const_cast<quic::QuicSession*>(session)
                                         ->config()
                                         ->GetInitialSessionFlowControlWindowToSend());
  EXPECT_EQ(stream_window_size_, const_cast<quic::QuicSession*>(session)
                                     ->config()
                                     ->GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  readFromClientSockets();
}

TEST_P(ActiveQuicListenerTest, ProcessBufferedChlos) {
  initialize();
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  const uint32_t count = (ActiveQuicListener::kNumSessionsToCreatePerLoop * 2) + 1;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_per_event_loop_read_limit")) {
    maybeConfigureMocks(count + 1);
    // Create 1 session to increase number of packet to read in the next read event.
    sendCHLO(quic::test::TestConnectionId());
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    EXPECT_NE(0u, quic_dispatcher_->NumSessions());
  } else {
    maybeConfigureMocks(count);
  }

  // Generate one more CHLO than can be processed immediately.
  for (size_t i = 1; i <= count; ++i) {
    sendCHLO(quic::test::TestConnectionId(i));
  }
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // The first kNumSessionsToCreatePerLoop were processed immediately, the next
  // kNumSessionsToCreatePerLoop were buffered for the next run of the event loop, and the last one
  // was buffered to the subsequent event loop.
  EXPECT_EQ(2, quic_listener_->eventLoopsWithBufferedChlosForTest());

  for (size_t i = 1; i <= count; ++i) {
    EXPECT_FALSE(buffered_packets->HasBufferedPackets(quic::test::TestConnectionId(i)));
  }
  EXPECT_FALSE(buffered_packets->HasChlosBuffered());
  EXPECT_NE(0u, quic_dispatcher_->NumSessions());
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.udp_per_event_loop_read_limit")) {
    EXPECT_EQ(50 * quic_dispatcher_->NumSessions(),
              quic_listener_->numPacketsExpectedPerEventLoop());
  }

  readFromClientSockets();
}

TEST_P(ActiveQuicListenerTest, QuicProcessingDisabledAndEnabled) {
  initialize();
  maybeConfigureMocks(/* connection_count = */ 2);
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
  sendCHLO(quic::test::TestConnectionId(1));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(quic_dispatcher_->NumSessions(), 1);

  Runtime::LoaderSingleton::getExisting()->mergeValues({{"quic.enabled", " false"}});
  sendCHLO(quic::test::TestConnectionId(2));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // If listener was enabled, there should have been session created for active connection.
  EXPECT_EQ(quic_dispatcher_->NumSessions(), 1);
  EXPECT_FALSE(ActiveQuicListenerPeer::enabled(*quic_listener_));

  Runtime::LoaderSingleton::getExisting()->mergeValues({{"quic.enabled", " true"}});
  sendCHLO(quic::test::TestConnectionId(2));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(quic_dispatcher_->NumSessions(), 2);
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
}

class ActiveQuicListenerEmptyFlagConfigTest : public ActiveQuicListenerTest {
protected:
  std::string yamlForQuicConfig() override {
    // Do not config flow control windows.
    return R"EOF(
    quic_protocol_options:
      max_concurrent_streams: 10
  )EOF";
  }
};

INSTANTIATE_TEST_SUITE_P(ActiveQuicListenerEmptyFlagConfigTests,
                         ActiveQuicListenerEmptyFlagConfigTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Quic listener should be enabled by default, if not enabled explicitly in config.
TEST_P(ActiveQuicListenerEmptyFlagConfigTest, ReceiveFullQuicCHLO) {
  initialize();
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  maybeConfigureMocks(/* connection_count = */ 1);
  quic::QuicConnectionId connection_id = quic::test::TestConnectionId(1);
  sendCHLO(connection_id);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(buffered_packets->HasChlosBuffered());
  EXPECT_NE(0u, quic_dispatcher_->NumSessions());
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
  const quic::QuicSession* session =
      quic::test::QuicDispatcherPeer::FindSession(quic_dispatcher_, connection_id);
  ASSERT(session != nullptr);
  // Check defaults stream and connection flow control window to send.
  EXPECT_EQ(Http3::Utility::OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
            const_cast<quic::QuicSession*>(session)
                ->config()
                ->GetInitialSessionFlowControlWindowToSend());
  EXPECT_EQ(Http3::Utility::OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
            const_cast<quic::QuicSession*>(session)
                ->config()
                ->GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  readFromClientSockets();
}

} // namespace Quic
} // namespace Envoy
