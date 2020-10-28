#include <cstdlib>
#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
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

#include "server/configuration_impl.h"
#include "common/common/logger.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/udp_packet_writer_handler_impl.h"
#include "common/runtime/runtime_impl.h"
#include "extensions/quic_listeners/quiche/active_quic_listener.h"
#include "test/extensions/quic_listeners/quiche/test_utils.h"
#include "test/extensions/quic_listeners/quiche/test_proof_source.h"
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
#include "extensions/quic_listeners/quiche/active_quic_listener_config.h"
#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"
#include "extensions/quic_listeners/quiche/envoy_quic_utils.h"
#include "extensions/quic_listeners/quiche/udp_gso_batch_writer.h"

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

  static bool enabled(ActiveQuicListener& listener) { return listener.enabled_.enabled(); }
};

class ActiveQuicListenerFactoryPeer {
public:
  static envoy::config::core::v3::RuntimeFeatureFlag&
  runtimeEnabled(ActiveQuicListenerFactory* factory) {
    return factory->enabled_;
  }
};

class ActiveQuicListenerTest : public QuicMultiVersionTest {
protected:
  using Socket =
      Network::NetworkListenSocket<Network::NetworkSocketTrait<Network::Socket::Type::Datagram>>;

  ActiveQuicListenerTest()
      : version_(GetParam().first), api_(Api::createApiForTest(simulated_time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), clock_(*dispatcher_),
        local_address_(Network::Test::getCanonicalLoopbackAddress(version_)),
        connection_handler_(*dispatcher_, absl::nullopt), quic_version_([]() {
          if (GetParam().second == QuicVersionType::GquicQuicCrypto) {
            return quic::CurrentSupportedVersionsWithQuicCrypto();
          }
          bool use_http3 = GetParam().second == QuicVersionType::Iquic;
          SetQuicReloadableFlag(quic_disable_version_draft_29, !use_http3);
          SetQuicReloadableFlag(quic_disable_version_draft_27, !use_http3);
          return quic::CurrentSupportedVersions();
        }()[0]) {}

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

    ON_CALL(listener_config_, listenSocketFactory()).WillByDefault(ReturnRef(socket_factory_));
    ON_CALL(socket_factory_, getListenSocket()).WillByDefault(Return(listen_socket_));

    // Use UdpGsoBatchWriter to perform non-batched writes for the purpose of this test, if it is
    // supported.
    ON_CALL(listener_config_, udpPacketWriterFactory())
        .WillByDefault(Return(
            std::reference_wrapper<Network::UdpPacketWriterFactory>(udp_packet_writer_factory_)));
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

    listener_factory_ = createQuicListenerFactory(yamlForQuicConfig());
    EXPECT_CALL(listener_config_, filterChainManager()).WillOnce(ReturnRef(filter_chain_manager_));
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
    std::string listener_name = QuicListenerName;
    auto& config_factory =
        Config::Utility::getAndCheckFactoryByName<Server::ActiveUdpListenerConfigFactory>(
            listener_name);
    ProtobufTypes::MessagePtr config_proto = config_factory.createEmptyConfigProto();
    TestUtility::loadFromYaml(yaml, *config_proto);
    return config_factory.createActiveUdpListenerFactory(*config_proto, /*concurrency=*/1);
  }

  void maybeConfigureMocks(int connection_count) {
    if (quic_version_.UsesTls()) {
      return;
    }
    EXPECT_CALL(listener_config_, filterChainFactory()).Times(connection_count);
    EXPECT_CALL(listener_config_.filter_chain_factory_, createNetworkFilterChain(_, _))
        .Times(connection_count)
        .WillRepeatedly(Invoke([](Network::Connection& connection,
                                  const std::vector<Network::FilterFactoryCb>& filter_factories) {
          EXPECT_EQ(1u, filter_factories.size());
          Server::Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories);
          return true;
        }));
    EXPECT_CALL(network_connection_callbacks_, onEvent(Network::ConnectionEvent::Connected))
        .Times(connection_count);
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
      EXPECT_CALL(*filter_chain_, networkFilterFactories())
          .InSequence(seq)
          .WillOnce(ReturnRef(filter_factories_.back()));
    }
  }

  void sendCHLO(quic::QuicConnectionId connection_id) {
    client_sockets_.push_back(std::make_unique<Socket>(local_address_, nullptr, /*bind*/ false));
    Buffer::OwnedImpl payload = generateChloPacketToSend(
        quic_version_, quic_config_, ActiveQuicListenerPeer::cryptoConfig(*quic_listener_),
        connection_id, clock_, envoyIpAddressToQuicSocketAddress(local_address_->ip()),
        envoyIpAddressToQuicSocketAddress(local_address_->ip()), "test.example.org");
    Buffer::RawSliceVector slice = payload.getRawSlices();
    ASSERT_EQ(1u, slice.size());
    // Send a full CHLO to finish 0-RTT handshake.
    auto send_rc = Network::Utility::writeToSocket(client_sockets_.back()->ioHandle(), slice.data(),
                                                   1, nullptr, *listen_socket_->localAddress());
    ASSERT_EQ(slice[0].len_, send_rc.rc_);

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
          bytes_read += result.rc_;
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
    return R"EOF(
    enabled:
      default_value: true
      runtime_key: quic.enabled
)EOF";
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

  std::list<std::unique_ptr<Socket>> client_sockets_;
  std::list<std::shared_ptr<Network::MockReadFilter>> read_filters_;
  Network::MockFilterChainManager filter_chain_manager_;
  // The following two containers must guarantee pointer stability as addresses
  // of elements are saved in expectations before new elements are added.
  std::list<std::vector<Network::FilterFactoryCb>> filter_factories_;
  const Network::MockFilterChain* filter_chain_;
  quic::ParsedQuicVersion quic_version_;
};

INSTANTIATE_TEST_SUITE_P(ActiveQuicListenerTests, ActiveQuicListenerTest,
                         testing::ValuesIn(generateTestParam()), testParamsToString);

TEST_P(ActiveQuicListenerTest, FailSocketOptionUponCreation) {
  auto option = std::make_unique<Network::MockSocketOption>();
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_BOUND))
      .WillOnce(Return(false));
  auto options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
  options->emplace_back(std::move(option));
  quic_listener_.reset();
  EXPECT_THROW_WITH_REGEX(
      (void)std::make_unique<ActiveQuicListener>(
          0, 1, *dispatcher_, connection_handler_, listen_socket_, listener_config_, quic_config_,
          options, false,
          ActiveQuicListenerFactoryPeer::runtimeEnabled(
              static_cast<ActiveQuicListenerFactory*>(listener_factory_.get()))),
      Network::CreateListenerException, "Failed to apply socket options.");
}

TEST_P(ActiveQuicListenerTest, ReceiveCHLO) {
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  maybeConfigureMocks(/* connection_count = */ 1);
  sendCHLO(quic::test::TestConnectionId(1));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(buffered_packets->HasChlosBuffered());
  EXPECT_FALSE(quic_dispatcher_->session_map().empty());
  readFromClientSockets();
}

TEST_P(ActiveQuicListenerTest, ProcessBufferedChlos) {
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  const uint32_t count = (ActiveQuicListener::kNumSessionsToCreatePerLoop * 2) + 1;
  maybeConfigureMocks(count);

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
  EXPECT_FALSE(quic_dispatcher_->session_map().empty());
  readFromClientSockets();
}

TEST_P(ActiveQuicListenerTest, QuicProcessingDisabledAndEnabled) {
  maybeConfigureMocks(/* connection_count = */ 2);
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
  sendCHLO(quic::test::TestConnectionId(1));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(quic_dispatcher_->session_map().size(), 1);

  Runtime::LoaderSingleton::getExisting()->mergeValues({{"quic.enabled", " false"}});
  sendCHLO(quic::test::TestConnectionId(2));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  // If listener was enabled, there should have been session created for active connection.
  EXPECT_EQ(quic_dispatcher_->session_map().size(), 1);
  EXPECT_FALSE(ActiveQuicListenerPeer::enabled(*quic_listener_));

  Runtime::LoaderSingleton::getExisting()->mergeValues({{"quic.enabled", " true"}});
  sendCHLO(quic::test::TestConnectionId(2));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(quic_dispatcher_->session_map().size(), 2);
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
}

class ActiveQuicListenerEmptyFlagConfigTest : public ActiveQuicListenerTest {
protected:
  std::string yamlForQuicConfig() override {
    return R"EOF(
    max_concurrent_streams: 10
  )EOF";
  }
};

INSTANTIATE_TEST_SUITE_P(ActiveQuicListenerEmptyFlagConfigTests,
                         ActiveQuicListenerEmptyFlagConfigTest,
                         testing::ValuesIn(generateTestParam()), testParamsToString);

// Quic listener should be enabled by default, if not enabled explicitly in config.
TEST_P(ActiveQuicListenerEmptyFlagConfigTest, ReceiveFullQuicCHLO) {
  quic::QuicBufferedPacketStore* const buffered_packets =
      quic::test::QuicDispatcherPeer::GetBufferedPackets(quic_dispatcher_);
  maybeConfigureMocks(/* connection_count = */ 1);
  sendCHLO(quic::test::TestConnectionId(1));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(buffered_packets->HasChlosBuffered());
  EXPECT_FALSE(quic_dispatcher_->session_map().empty());
  EXPECT_TRUE(ActiveQuicListenerPeer::enabled(*quic_listener_));
  readFromClientSockets();
}

} // namespace Quic
} // namespace Envoy
