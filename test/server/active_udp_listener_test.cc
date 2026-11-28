#include <memory>

#include "envoy/common/exception.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "source/common/common/callback_impl.h"
#include "source/common/filter/udp_listener_filter.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"
#include "source/server/active_udp_listener.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

class MockUdpConnectionHandler : public Network::UdpConnectionHandler,
                                 public Network::MockConnectionHandler {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::UdpListenerCallbacksOptRef, getUdpListenerCallbacks,
              (uint64_t listener_tag, const Network::Address::Instance& address));
};

class TestActiveRawUdpListener : public ActiveRawUdpListener {
public:
  TestActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                           Network::UdpConnectionHandler& parent,
                           Network::SocketSharedPtr listen_socket_ptr,
                           Event::Dispatcher& dispatcher, Network::ListenerConfig& config)
      : ActiveRawUdpListener(worker_index, concurrency, parent, listen_socket_ptr, dispatcher,
                             config),
        destination_(worker_index) {}
  uint32_t destination(const Network::UdpRecvData&) const override { return destination_; }

  uint32_t destination_;
};

class WorkerConfigProvider
    : public Config::ExtensionConfigProvider<Network::UdpListenerFilterFactoryCb> {
public:
  using Factory = Network::UdpListenerFilterFactoryCb;

  const std::string& name() override { return name_; }
  OptRef<Factory> config() override { return factory_ ? OptRef<Factory>(*factory_) : std::nullopt; }
  Common::CallbackHandlePtr addUpdateCallback(std::function<void()> callback) override {
    return updates_.add(std::move(callback));
  }
  void update(std::optional<Factory> factory) {
    factory_ = std::move(factory);
    updates_.runCallbacks();
  }

  Common::CallbackManager<void> updates_;

private:
  const std::string name_{"test_filter"};
  std::optional<Factory> factory_;
};

struct FilterActivity {
  uint32_t datagrams_{};
  uint32_t errors_{};
  uint32_t destructions_{};
};

class RecordingFilter : public Network::UdpListenerReadFilter {
public:
  RecordingFilter(Network::UdpReadFilterCallbacks& callbacks, FilterActivity& activity)
      : UdpListenerReadFilter(callbacks), activity_(activity) {}
  ~RecordingFilter() override { ++activity_.destructions_; }

  Network::FilterStatus onData(Network::UdpRecvData&) override {
    ++activity_.datagrams_;
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    ++activity_.errors_;
    return Network::FilterStatus::Continue;
  }

  static WorkerConfigProvider::Factory factory(FilterActivity& activity) {
    return [&activity](Network::UdpListenerFilterManager& manager,
                       Network::UdpReadFilterCallbacks& callbacks) {
      manager.addReadFilter(std::make_unique<RecordingFilter>(callbacks, activity));
    };
  }

private:
  FilterActivity& activity_;
};

class ActiveUdpListenerTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveUdpListenerTest()
      : version_(GetParam()), local_address_(Network::Test::getCanonicalLoopbackAddress(version_)) {
  }

  void setup(uint32_t concurrency = 1) {
    udp_listener_config_ = std::make_unique<NiceMock<Network::MockUdpListenerConfig>>(2);
    ON_CALL(conn_handler_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));

    listen_socket_ =
        std::make_shared<Network::UdpListenSocket>(local_address_, nullptr, /*bind*/ true);
    listen_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    listen_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    ASSERT_TRUE(Network::Socket::applyOptions(listen_socket_->options(), *listen_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));

    ON_CALL(*static_cast<Network::MockListenSocketFactory*>(
                listener_config_.socket_factories_[0].get()),
            getListenSocket(_))
        .WillByDefault(Return(listen_socket_));

    // Use UdpGsoBatchWriter to perform non-batched writes for the purpose of this test, if it is
    // supported.
    EXPECT_CALL(listener_config_, udpListenerConfig())
        .WillRepeatedly(Return(Network::UdpListenerConfigOptRef(*udp_listener_config_)));
    EXPECT_CALL(listener_config_, listenerScope()).WillRepeatedly(ReturnRef(scope_));
    EXPECT_CALL(listener_config_, filterChainFactory());
    ON_CALL(*udp_listener_config_, packetWriterFactory())
        .WillByDefault(ReturnRef(udp_packet_writer_factory_));
    ON_CALL(udp_packet_writer_factory_, createUdpPacketWriter(_, _, _, _))
        .WillByDefault([&](Network::IoHandle& io_handle, Stats::Scope& scope,
                           Envoy::Event::Dispatcher&,
                           absl::AnyInvocable<void() &&>) -> Network::UdpPacketWriterPtr {
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
          return std::make_unique<Quic::UdpGsoBatchWriter>(io_handle, scope);
#else
          UNREFERENCED_PARAMETER(scope);
          return std::make_unique<Network::UdpDefaultWriter>(io_handle);
#endif
        });

    EXPECT_CALL(cb_.udp_listener_, onDestroy());

    active_listener_ = std::make_unique<TestActiveRawUdpListener>(
        0, concurrency, conn_handler_, listen_socket_, dispatcher_, listener_config_);
  }

  Network::UdpRecvData makeRecvData(uint32_t peer_port) {
    Network::UdpRecvData data;
    data.addresses_.local_ = listen_socket_->connectionInfoProvider().localAddress();
    data.addresses_.peer_ = Network::Utility::getAddressWithPort(*local_address_, peer_port);
    return data;
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  NiceMock<MockUdpConnectionHandler> conn_handler_;
  Network::Address::IpVersion version_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::SocketSharedPtr listen_socket_;
  NiceMock<Network::MockListenSocketFactory> socket_factory_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::unique_ptr<NiceMock<Network::MockUdpListenerConfig>> udp_listener_config_;
  NiceMock<Network::MockUdpPacketWriterFactory> udp_packet_writer_factory_;
  Network::MockListenerConfig listener_config_;
  std::unique_ptr<TestActiveRawUdpListener> active_listener_;
  NiceMock<Network::MockUdpReadFilterCallbacks> cb_;
};

INSTANTIATE_TEST_SUITE_P(ActiveUdpListenerTests, ActiveUdpListenerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ActiveUdpListenerTest, DynamicFiltersGateTheChainUntilAllConfigsAreReady) {
  WorkerConfigProvider first;
  WorkerConfigProvider second;
  FilterActivity before, first_v1, first_v2, second_activity;
  setup();
  auto listener = std::move(active_listener_);
  auto& failures = scope_.counterFromString("udp.extension_config_update_failed");
  listener->addReadFilter(std::make_unique<RecordingFilter>(*listener, before));
  listener->addReadFilter(std::make_unique<Filter::UdpListenerFilter>(first, *listener, failures));
  listener->addReadFilter(std::make_unique<Filter::UdpListenerFilter>(second, *listener, failures));
  auto dispatch = [&]() {
    listener->onDataWorker(makeRecvData(1000));
    listener->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
  };

  dispatch();
  first.update(RecordingFilter::factory(first_v1));
  dispatch();
  EXPECT_EQ(0, before.datagrams_);
  EXPECT_EQ(0, before.errors_);
  EXPECT_EQ(0, first_v1.datagrams_);

  second.update(RecordingFilter::factory(second_activity));
  dispatch();
  EXPECT_EQ(1, first_v1.datagrams_);
  EXPECT_EQ(1, first_v1.errors_);
  first.update(RecordingFilter::factory(first_v2));
  dispatch();
  EXPECT_EQ(1, first_v1.destructions_);
  EXPECT_EQ(1, first_v2.datagrams_);
  EXPECT_EQ(2, second_activity.datagrams_);
  EXPECT_EQ(0, before.destructions_);
  EXPECT_EQ(0, second_activity.destructions_);

  second.update(std::nullopt);
  second.update(std::nullopt);
  dispatch();
  EXPECT_EQ(2, before.datagrams_);
  EXPECT_EQ(2, before.errors_);
  EXPECT_EQ(1, first_v2.errors_);
  EXPECT_EQ(1, second_activity.destructions_);

  second.update(RecordingFilter::factory(second_activity));
  dispatch();
  EXPECT_EQ(3, before.datagrams_);
  EXPECT_EQ(2, first_v2.datagrams_);
  EXPECT_EQ(3, second_activity.datagrams_);
  EXPECT_EQ(3, scope_.counterFromString("udp.extension_config_missing").value());
  EXPECT_EQ(0, failures.value());

  listener->shutdownListener({});
  EXPECT_EQ(0, first.updates_.size());
  EXPECT_EQ(0, second.updates_.size());
  EXPECT_EQ(1, first_v2.destructions_);
  EXPECT_EQ(2, second_activity.destructions_);
}

TEST_P(ActiveUdpListenerTest, FailedWorkerUpdateDiscardsPartialFiltersAndRecovers) {
  WorkerConfigProvider provider;
  FilterActivity before, original, partial, recovered;
  provider.update(RecordingFilter::factory(original));
  setup();
  auto listener = std::move(active_listener_);
  auto& failures = scope_.counterFromString("udp.extension_config_update_failed");
  listener->addReadFilter(std::make_unique<RecordingFilter>(*listener, before));
  listener->addReadFilter(
      std::make_unique<Filter::UdpListenerFilter>(provider, *listener, failures));
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(1, original.datagrams_);

  provider.update([&partial](Network::UdpListenerFilterManager& manager,
                             Network::UdpReadFilterCallbacks& callbacks) {
    manager.addReadFilter(std::make_unique<RecordingFilter>(callbacks, partial));
    throw EnvoyException("worker construction failed");
  });
  EXPECT_EQ(1, original.destructions_);
  EXPECT_EQ(1, partial.destructions_);
  EXPECT_EQ(1, failures.value());
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(1, before.datagrams_);
  EXPECT_EQ(1, original.datagrams_);
  EXPECT_EQ(0, partial.datagrams_);
  EXPECT_EQ(1, scope_.counterFromString("udp.extension_config_missing").value());

  provider.update(RecordingFilter::factory(recovered));
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(2, before.datagrams_);
  EXPECT_EQ(1, recovered.datagrams_);
  EXPECT_EQ(1, failures.value());
}

TEST_P(ActiveUdpListenerTest, EmptyAndNullWorkerFiltersBlockDatagramsUntilRecovery) {
  WorkerConfigProvider provider;
  FilterActivity before, partial, recovered;
  setup();
  auto listener = std::move(active_listener_);
  auto& failures = scope_.counterFromString("udp.extension_config_update_failed");
  auto& missing = scope_.counterFromString("udp.extension_config_missing");
  listener->addReadFilter(std::make_unique<RecordingFilter>(*listener, before));

  provider.update([](Network::UdpListenerFilterManager&, Network::UdpReadFilterCallbacks&) {});
  listener->addReadFilter(
      std::make_unique<Filter::UdpListenerFilter>(provider, *listener, failures));
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(1, failures.value());
  EXPECT_EQ(1, missing.value());
  EXPECT_EQ(0, before.datagrams_);

  provider.update([&partial](Network::UdpListenerFilterManager& manager,
                             Network::UdpReadFilterCallbacks& callbacks) {
    manager.addReadFilter(std::make_unique<RecordingFilter>(callbacks, partial));
    manager.addReadFilter(nullptr);
  });
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(2, failures.value());
  EXPECT_EQ(2, missing.value());
  EXPECT_EQ(0, before.datagrams_);
  EXPECT_EQ(0, partial.datagrams_);
  EXPECT_EQ(1, partial.destructions_);

  provider.update(RecordingFilter::factory(recovered));
  listener->onDataWorker(makeRecvData(1000));
  EXPECT_EQ(1, before.datagrams_);
  EXPECT_EQ(1, recovered.datagrams_);
  EXPECT_EQ(2, missing.value());
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnData) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).WillOnce([](Network::UdpRecvData&) -> Network::FilterStatus {
    return Network::FilterStatus::Continue;
  });
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onData(_))
      .WillOnce([](Network::UdpRecvData&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      });

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onDataWorker(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnDataStopIteration) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).WillOnce([](Network::UdpRecvData&) -> Network::FilterStatus {
    return Network::FilterStatus::StopIteration;
  });
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onData(_)).Times(0);

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onDataWorker(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnReceiveError) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onReceiveError(_))
      .WillOnce([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::Continue;
      });
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onReceiveError(_))
      .WillOnce([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      });

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnReceiveErrorStopIteration) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onReceiveError(_))
      .WillOnce([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      });
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onReceiveError(_)).Times(0);

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

TEST_P(ActiveUdpListenerTest, HotRestartShutdownForwardsUnknownSessions) {
  setup();

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  // Only the post-shutdown datagram of the registered session is served locally.
  EXPECT_CALL(*test_filter, onData(_));
  active_listener_->addReadFilter(std::move(test_filter));

  // A filter registers a session, as udp_proxy would on the first datagram.
  auto session = makeRecvData(1000);
  auto handle = active_listener_->registerHotRestartSession(session.addresses_.local_,
                                                            session.addresses_.peer_);

  Network::MockNonDispatchedUdpPacketHandler packet_handler;
  Network::ExtraShutdownListenerOptions options;
  options.non_dispatched_udp_packet_handler_ = packet_handler;
  active_listener_->shutdownListener(options);
  EXPECT_NE(active_listener_->listener(), nullptr);

  // The registered session is still served locally.
  active_listener_->onData(makeRecvData(1000));

  // An unregistered session is forwarded to the child instance without creating local state,
  // so a repeated datagram is forwarded again.
  EXPECT_CALL(packet_handler, handle(0, _, _)).Times(3);
  active_listener_->onData(makeRecvData(2000));
  active_listener_->onData(makeRecvData(2000));

  // After the handle is dropped, the session is forwarded too.
  handle.reset();
  active_listener_->onData(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, HotRestartShutdownGuardDisabledServesLocally) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.udp_hot_restart_session_handoff", "false"}});
  setup();

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  // With the guard disabled the draining parent serves every datagram locally, registered or not.
  EXPECT_CALL(*test_filter, onData(_)).Times(2);
  active_listener_->addReadFilter(std::move(test_filter));

  Network::MockNonDispatchedUdpPacketHandler packet_handler;
  EXPECT_CALL(packet_handler, handle(_, _, _)).Times(0);
  Network::ExtraShutdownListenerOptions options;
  options.non_dispatched_udp_packet_handler_ = packet_handler;
  active_listener_->shutdownListener(options);

  EXPECT_NE(active_listener_->listener(), nullptr);
  active_listener_->onData(makeRecvData(1000));
  active_listener_->onData(makeRecvData(2000));
}

TEST_P(ActiveUdpListenerTest, RegularShutdownDestroysListener) {
  setup();

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).Times(0);
  active_listener_->addReadFilter(std::move(test_filter));

  active_listener_->shutdownListener({});
  EXPECT_EQ(active_listener_->listener(), nullptr);

  // Packets after teardown are dropped.
  active_listener_->onData(makeRecvData(1000));
}

// Drain notifications are broadcast to every listener the connection handler owns, so a UDP
// listener receives them on any server drain or hot restart. They must be silent no-ops rather
// than ENVOY_BUGs. See ListenerManagerImpl::onServerDrainStart().
TEST_P(ActiveUdpListenerTest, DrainNotificationsAreNoOps) {
  setup();

  active_listener_->onListenerDrainStart(Network::ConnectionDrainEvent{});
  const std::list<const Network::FilterChain*> filter_chains;
  active_listener_->onFilterChainDrainStart(filter_chains, Network::ConnectionDrainEvent{});
}

} // namespace
} // namespace Server
} // namespace Envoy
