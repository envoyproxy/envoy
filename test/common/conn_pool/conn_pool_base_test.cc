#include "source/common/conn_pool/conn_pool_base.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace ConnectionPool {

using testing::AnyNumber;
using testing::HasSubstr;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

class TestActiveClient : public ActiveClient {
public:
  TestActiveClient(ConnPoolImplBase& parent, uint32_t lifetime_stream_limit,
                   uint32_t concurrent_stream_limit, bool supports_early_data)
      : ActiveClient(parent, lifetime_stream_limit, concurrent_stream_limit),
        supports_early_data_(supports_early_data) {}

  void close() override { onEvent(Network::ConnectionEvent::LocalClose); }
  uint64_t id() const override { return 1; }
  bool closingWithIncompleteStream() const override { return false; }
  uint32_t numActiveStreams() const override { return active_streams_; }
  absl::optional<Http::Protocol> protocol() const override { return absl::nullopt; }
  void onEvent(Network::ConnectionEvent event) override {
    parent_.onConnectionEvent(*this, "", event);
  }

  static void incrementActiveStreams(ActiveClient& client) {
    TestActiveClient* testClient = dynamic_cast<TestActiveClient*>(&client);
    ASSERT_TRUE(testClient != nullptr);
    testClient->active_streams_++;
  }
  int64_t currentUnusedCapacity() const override {
    if (capacity_override_.has_value()) {
      return capacity_override_.value();
    }
    return ActiveClient::currentUnusedCapacity();
  }

  bool readyForStream() const override {
    if (!supports_early_data_) {
      return ActiveClient::readyForStream();
    }
    return state() == ActiveClient::State::Ready ||
           state() == ActiveClient::State::ReadyForEarlyData;
  }

  bool hasHandshakeCompleted() const override {
    if (!supports_early_data_) {
      return ActiveClient::hasHandshakeCompleted();
    }
    return has_handshake_completed_;
  }

  bool supportsEarlyData() const override { return supports_early_data_; }
  uint32_t active_streams_{};

  absl::optional<uint64_t> capacity_override_;

private:
  bool supports_early_data_;
};

class TestPendingStream : public PendingStream {
public:
  TestPendingStream(ConnPoolImplBase& parent, AttachContext& context, bool can_send_early_data)
      : PendingStream(parent, can_send_early_data), context_(context) {}
  AttachContext& context() override { return context_; }
  AttachContext& context_;
};

class TestConnPoolImplBase : public ConnPoolImplBase {
public:
  using ConnPoolImplBase::ConnPoolImplBase;
  ConnectionPool::Cancellable* newPendingStream(AttachContext& context,
                                                bool can_send_early_data) override {
    auto entry = std::make_unique<TestPendingStream>(*this, context, can_send_early_data);
    return addPendingStream(std::move(entry));
  }
  MOCK_METHOD(ActiveClientPtr, instantiateActiveClient, ());
  MOCK_METHOD(void, onPoolFailure,
              (const Upstream::HostDescriptionConstSharedPtr& n, absl::string_view,
               ConnectionPool::PoolFailureReason, AttachContext&));
  MOCK_METHOD(void, onPoolReady, (ActiveClient&, AttachContext&));
};

class ConnPoolImplBaseTest : public testing::Test {
public:
  ConnPoolImplBaseTest()
      : upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        pool_(host_, Upstream::ResourcePriority::Default, dispatcher_, nullptr, nullptr, state_) {
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
    ON_CALL(pool_, instantiateActiveClient).WillByDefault(Invoke([&]() -> ActiveClientPtr {
      auto ret = std::make_unique<NiceMock<TestActiveClient>>(
          pool_, stream_limit_, concurrent_streams_, /*supports_early_data=*/false);
      clients_.push_back(ret.get());
      ret->real_host_description_ = descr_;
      return ret;
    }));
    ON_CALL(pool_, onPoolReady(_, _))
        .WillByDefault(Invoke([](ActiveClient& client, AttachContext&) {
          TestActiveClient::incrementActiveStreams(client);
        }));
  }

#define CHECK_STATE(active, pending, capacity)                                                     \
  EXPECT_EQ(state_.pending_streams_, pending);                                                     \
  EXPECT_EQ(state_.active_streams_, active);                                                       \
  EXPECT_EQ(state_.connecting_and_connected_stream_capacity_, capacity)

  uint32_t stream_limit_ = 100;
  uint32_t concurrent_streams_ = 1;
  Upstream::ClusterConnectivityState state_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> descr_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  Upstream::HostSharedPtr host_{
      Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80", dispatcher_.timeSource())};
  TestConnPoolImplBase pool_;
  AttachContext context_;
  std::vector<TestActiveClient*> clients_;
};

class ConnPoolImplDispatcherBaseTest : public testing::Test {
public:
  ConnPoolImplDispatcherBaseTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        pool_(host_, Upstream::ResourcePriority::Default, *dispatcher_, nullptr, nullptr, state_) {
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
    ON_CALL(pool_, instantiateActiveClient).WillByDefault(Invoke([&]() -> ActiveClientPtr {
      auto ret = std::make_unique<NiceMock<TestActiveClient>>(
          pool_, stream_limit_, concurrent_streams_, clients_support_early_data_);
      clients_.push_back(ret.get());
      ret->real_host_description_ = descr_;
      return ret;
    }));
    ON_CALL(pool_, onPoolReady(_, _))
        .WillByDefault(Invoke([](ActiveClient& client, AttachContext&) {
          TestActiveClient::incrementActiveStreams(client);
        }));
  }

  void newConnectingClient() {
    ON_CALL(*cluster_, maxConnectionDuration).WillByDefault(Return(max_connection_duration_opt_));

    // Create a new stream using the pool
    EXPECT_CALL(pool_, instantiateActiveClient);
    pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
    ASSERT_EQ(1, clients_.size());
    EXPECT_EQ(ActiveClient::State::Connecting, clients_.back()->state());

    // Verify that the connection duration timer isn't set yet. This shouldn't happen
    // until after connect.
    EXPECT_EQ(nullptr, clients_.back()->connection_duration_timer_);
  }

  void newActiveClientAndStream(ActiveClient::State expected_state = ActiveClient::State::Busy) {
    // Start with a connecting client
    newConnectingClient();

    // Connect and expect the expected state.
    EXPECT_CALL(pool_, onPoolReady);
    clients_.back()->onEvent(Network::ConnectionEvent::Connected);
    EXPECT_EQ(expected_state, clients_.back()->state());

    // Verify that the connect duration timer is consistent with the max connection duration opt
    if (max_connection_duration_opt_.has_value()) {
      EXPECT_TRUE(clients_.back()->connection_duration_timer_ != nullptr);
      EXPECT_TRUE(clients_.back()->connection_duration_timer_->enabled());
    } else {
      EXPECT_EQ(nullptr, clients_.back()->connection_duration_timer_);
    }
  }

  void newDrainingClient() {
    // Use a stream limit of 1 to force draining. Then, connect and expect draining.
    stream_limit_ = 1;
    newActiveClientAndStream(ActiveClient::State::Draining);
  }

  void newClosedClient() {
    // Start with a draining client. Then, close the stream. This will result in the client being
    // closed.
    newDrainingClient();
    closeStream();
  }

  // Advance time and block until the next event
  void advanceTimeAndRun(uint32_t duration_ms) {
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(duration_ms), *dispatcher_,
                                   Event::Dispatcher::RunType::Block);
  }

  // Close the active stream
  void closeStream() {
    while (clients_.back()->active_streams_ > 0) {
      --clients_.back()->active_streams_;
      pool_.onStreamClosed(*clients_.back(), false);
    }
  }

  void closeStreamAndDrainClient() {
    // Close the active stream and expect the client to be ready.
    closeStream();
    EXPECT_EQ(ActiveClient::State::Ready, clients_.back()->state());

    // The client is still ready. So, to clean up, we have to drain the pool manually.
    pool_.drainConnectionsImpl(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  }

  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  uint32_t max_connection_duration_ = 5000;
  absl::optional<std::chrono::milliseconds> max_connection_duration_opt_{max_connection_duration_};
  uint32_t stream_limit_ = 100;
  uint32_t concurrent_streams_ = 1;
  Upstream::ClusterConnectivityState state_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> descr_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{
      Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80", dispatcher_->timeSource())};
  TestConnPoolImplBase pool_;
  AttachContext context_;
  std::vector<TestActiveClient*> clients_;
  bool clients_support_early_data_{false};
};

TEST_F(ConnPoolImplBaseTest, DumpState) {
  std::stringstream out;
  pool_.dumpState(out, 0);
  std::string state = out.str();
  EXPECT_THAT(state, HasSubstr("ready_clients_.size(): 0, busy_clients_.size(): 0, "
                               "connecting_clients_.size(): 0, connecting_stream_capacity_: 0, "
                               "num_active_streams_: 0"));
}

TEST_F(ConnPoolImplBaseTest, BasicPreconnect) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*connecting capacity*/);
  EXPECT_CALL(pool_, instantiateActiveClient).Times(2);
  auto cancelable = pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*connecting capacity*/);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 1 /*connecting capacity*/);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, PreconnectOnDisconnect) {
  testing::InSequence s;

  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  EXPECT_CALL(pool_, instantiateActiveClient).Times(2);
  pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*connecting capacity*/);

  // If a connection fails, existing connections are purged. If a retry causes
  // a new stream, make sure we create the correct number of connections.
  EXPECT_CALL(pool_, onPoolFailure).WillOnce(InvokeWithoutArgs([&]() -> void {
    pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  }));
  EXPECT_CALL(pool_, instantiateActiveClient);
  clients_[0]->close();
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*connecting capacity*/);

  EXPECT_CALL(pool_, onPoolFailure);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPreconnectIfUnhealthy) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  host_->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(host_->coarseHealth(), Upstream::Host::Health::Unhealthy);

  // On new stream, create 1 connection.
  EXPECT_CALL(pool_, instantiateActiveClient);
  auto cancelable = pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 1 /*connecting capacity*/);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPreconnectIfDegraded) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  EXPECT_EQ(host_->coarseHealth(), Upstream::Host::Health::Healthy);
  host_->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_EQ(host_->coarseHealth(), Upstream::Host::Health::Degraded);

  // On new stream, create 1 connection.
  EXPECT_CALL(pool_, instantiateActiveClient);
  auto cancelable = pool_.newStreamImpl(context_, /*can_send_early_data=*/false);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, ExplicitPreconnect) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));
  EXPECT_CALL(pool_, instantiateActiveClient).Times(AnyNumber());

  // With global preconnect off, we won't preconnect.
  EXPECT_FALSE(pool_.maybePreconnectImpl(0));
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*connecting capacity*/);
  // With preconnect ratio of 1.1, we'll preconnect two connections.
  // Currently, no number of subsequent calls to preconnect will increase that.
  EXPECT_TRUE(pool_.maybePreconnectImpl(1.1));
  EXPECT_TRUE(pool_.maybePreconnectImpl(1.1));
  EXPECT_FALSE(pool_.maybePreconnectImpl(1.1));
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 2 /*connecting capacity*/);

  // With a higher preconnect ratio, more connections may be preconnected.
  EXPECT_TRUE(pool_.maybePreconnectImpl(3));

  pool_.destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, ExplicitPreconnectNotHealthy) {
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // Preconnect won't occur if the host is not healthy.
  host_->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_FALSE(pool_.maybePreconnectImpl(1));
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationTimerNull) {
  // Force a null max connection duration optional.
  // newActiveClientAndStream() will expect the connection duration timer to remain null.
  max_connection_duration_opt_ = absl::nullopt;
  newActiveClientAndStream();
  closeStreamAndDrainClient();
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationTimerEnabled) {
  // Use the default max connection duration opt.
  // newActiveClientAndStream() will expect the connection duration timer to be non-null.
  newActiveClientAndStream();
  closeStreamAndDrainClient();
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationBusy) {
  newActiveClientAndStream();

  // Verify that advancing to just before the connection duration timeout doesn't drain the
  // connection.
  advanceTimeAndRun(max_connection_duration_ - 1);
  EXPECT_EQ(0, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
  EXPECT_EQ(ActiveClient::State::Busy, clients_.back()->state());

  // Verify that advancing past the connection duration timeout drains the connection,
  // because there's a busy client.
  advanceTimeAndRun(2);
  EXPECT_EQ(1, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
  EXPECT_EQ(ActiveClient::State::Draining, clients_.back()->state());
  closeStream();
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationReady) {
  newActiveClientAndStream();

  // Close active stream and expect that the client goes back to ready
  closeStream();
  EXPECT_EQ(ActiveClient::State::Ready, clients_.back()->state());

  // Verify that advancing to just before the connection duration timeout doesn't close the
  // connection.
  advanceTimeAndRun(max_connection_duration_ - 1);
  EXPECT_EQ(0, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
  EXPECT_EQ(ActiveClient::State::Ready, clients_.back()->state());

  // Verify that advancing past the connection duration timeout closes the connection,
  // because there's nothing to drain.
  advanceTimeAndRun(2);
  EXPECT_EQ(1, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationAlreadyDraining) {
  // Start with a client that is already draining.
  newDrainingClient();

  // Verify that advancing past the connection duration timeout does nothing to an active client
  // that is already draining.
  advanceTimeAndRun(max_connection_duration_ + 1);
  EXPECT_EQ(0, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
  EXPECT_EQ(ActiveClient::State::Draining, clients_.back()->state());
  closeStream();
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationAlreadyClosed) {
  // Start with a client that is already closed.
  newClosedClient();

  // Verify that advancing past the connection duration timeout does nothing to the active
  // client that is already closed.
  advanceTimeAndRun(max_connection_duration_ + 1);
  EXPECT_EQ(0, pool_.host()->cluster().trafficStats().upstream_cx_max_duration_reached_.value());
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationCallbackWhileClosedBug) {
  // Start with a connecting client
  newClosedClient();

  // Expect an ENVOY_BUG if the connection duration callback fires while in the Closed state.
  // We forcibly call the connection duration callback here because under normal circumstances there
  // is no timer set up.
  EXPECT_ENVOY_BUG(clients_.back()->onConnectionDurationTimeout(),
                   "max connection duration reached while closed");
}

TEST_F(ConnPoolImplDispatcherBaseTest, MaxConnectionDurationCallbackWhileConnectingBug) {
  // Start with a connecting client
  newConnectingClient();

  // Expect an ENVOY_BUG if the connection duration callback fires while still in the Connecting
  // state. We forcibly call the connection duration callback here because under normal
  // circumstances there is no timer set up.
  EXPECT_ENVOY_BUG(clients_.back()->onConnectionDurationTimeout(),
                   "max connection duration reached while connecting");

  // Finish the test as if the connection was never successful.
  EXPECT_CALL(pool_, onPoolFailure);
  pool_.destructAllConnections();
}

// Test the behavior of a client created with 0 zero streams available.
TEST_F(ConnPoolImplDispatcherBaseTest, NoAvailableStreams) {
  // Start with a concurrent stream limit of 0.
  stream_limit_ = 1;
  newConnectingClient();
  clients_.back()->capacity_override_ = 0;
  pool_.decrConnectingAndConnectedStreamCapacity(stream_limit_, *clients_.back());

  // Make sure that when the connected event is raised, there is no call to
  // onPoolReady, and the client is marked as busy.
  EXPECT_CALL(pool_, onPoolReady).Times(0);
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);
  EXPECT_EQ(ActiveClient::State::Busy, clients_.back()->state());

  // Clean up.
  EXPECT_CALL(pool_, instantiateActiveClient);
  EXPECT_CALL(pool_, onPoolFailure);
  pool_.destructAllConnections();
}

// Remote close simulates the peer closing the connection.
TEST_F(ConnPoolImplBaseTest, PoolIdleCallbackTriggeredRemoteClose) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(AnyNumber());

  // Create a new stream using the pool
  EXPECT_CALL(pool_, instantiateActiveClient);
  pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  ASSERT_EQ(1, clients_.size());

  // Emulate the new upstream connection establishment
  EXPECT_CALL(pool_, onPoolReady);
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);

  // The pool now has no requests/streams, but has an open connection, so it is not yet idle.
  clients_.back()->active_streams_ = 0;
  pool_.onStreamClosed(*clients_.back(), false);

  // Now that the last connection is closed, while there are no requests, the pool becomes idle.
  testing::MockFunction<void()> idle_pool_callback;
  EXPECT_CALL(idle_pool_callback, Call());
  pool_.addIdleCallbackImpl(idle_pool_callback.AsStdFunction());
  dispatcher_.clearDeferredDeleteList();
  clients_.back()->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(idle_pool_callback, Call());
  pool_.drainConnectionsImpl(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
}

// Local close simulates what would happen for an idle timeout on a connection.
TEST_F(ConnPoolImplBaseTest, PoolIdleCallbackTriggeredLocalClose) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).Times(AnyNumber());

  // Create a new stream using the pool
  EXPECT_CALL(pool_, instantiateActiveClient);
  pool_.newStreamImpl(context_, /*can_send_early_data=*/false);
  ASSERT_EQ(1, clients_.size());

  // Emulate the new upstream connection establishment
  EXPECT_CALL(pool_, onPoolReady);
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);

  // The pool now has no requests/streams, but has an open connection, so it is not yet idle.
  clients_.back()->active_streams_ = 0;
  pool_.onStreamClosed(*clients_.back(), false);

  // Now that the last connection is closed, while there are no requests, the pool becomes idle.
  testing::MockFunction<void()> idle_pool_callback;
  EXPECT_CALL(idle_pool_callback, Call());
  pool_.addIdleCallbackImpl(idle_pool_callback.AsStdFunction());
  dispatcher_.clearDeferredDeleteList();
  clients_.back()->onEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_CALL(idle_pool_callback, Call());
  pool_.drainConnectionsImpl(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
}

TEST_F(ConnPoolImplDispatcherBaseTest, ClientNotSupportEarlyDataGetsEarlyDataReady) {
  clients_support_early_data_ = false;
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1));

  EXPECT_CALL(pool_, instantiateActiveClient);
  Cancellable* cancelable = pool_.newStreamImpl(context_, /*can_send_early_data=*/true);
  EXPECT_NE(nullptr, cancelable);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, concurrent_streams_ /*connecting capacity*/);

  ActiveClient& client_ref = *clients_.back();
  // The first stream should be attached a client upon 0-RTT connected.
  EXPECT_CALL(pool_, onPoolReady).Times(0u);
  EXPECT_ENVOY_BUG(
      client_ref.onEvent(Network::ConnectionEvent::ConnectedZeroRtt),
      "Unable to set state to ReadyForEarlyData in a client which does not support early data");
  CHECK_STATE(0 /*active*/, 1 /*pending*/, concurrent_streams_ /*connecting capacity*/);

  // Clean up.
  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
}

TEST_F(ConnPoolImplDispatcherBaseTest, ConnectedZeroRttSendsEarlyData) {
  clients_support_early_data_ = true;
  concurrent_streams_ = 2u;
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1));

  EXPECT_CALL(pool_, instantiateActiveClient);
  EXPECT_NE(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/true));

  ActiveClient& client_ref = *clients_.back();
  // The first stream should be attached a client upon 0-RTT connected.
  EXPECT_CALL(pool_, onPoolReady);
  client_ref.onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  EXPECT_TRUE(client_ref.readyForStream());
  pool_.onUpstreamReadyForEarlyData(client_ref);

  CHECK_STATE(1 /*active*/, 0 /*pending*/, concurrent_streams_ - 1 /*connecting capacity*/);
  EXPECT_EQ(1, pool_.host()->cluster().trafficStats().upstream_rq_0rtt_.value());

  EXPECT_NE(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/false));
  CHECK_STATE(1 /*active*/, 1 /*pending*/, concurrent_streams_ - 1 /*connecting capacity*/);

  EXPECT_CALL(pool_, onPoolReady);
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);

  CHECK_STATE(2 /*active*/, 0 /*pending*/, 0 /*connecting capacity*/);
  EXPECT_EQ(1, pool_.host()->cluster().trafficStats().upstream_rq_0rtt_.value());

  // Clean up.
  closeStreamAndDrainClient();
}

TEST_F(ConnPoolImplDispatcherBaseTest, EarlyDataStreamsReachConcurrentStreamLimit) {
  clients_support_early_data_ = true;
  concurrent_streams_ = 2u;
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1));

  EXPECT_CALL(pool_, instantiateActiveClient);
  EXPECT_NE(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/true));

  ActiveClient& client_ref = *clients_.back();
  // The first stream should be attached a client upon 0-RTT connected.
  EXPECT_CALL(pool_, onPoolReady);
  client_ref.onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  EXPECT_TRUE(client_ref.readyForStream());
  pool_.onUpstreamReadyForEarlyData(client_ref);

  CHECK_STATE(1 /*active*/, 0 /*pending*/, concurrent_streams_ - 1 /*connecting capacity*/);
  EXPECT_EQ(1, pool_.host()->cluster().trafficStats().upstream_rq_0rtt_.value());

  EXPECT_CALL(pool_, onPoolReady);
  EXPECT_EQ(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/true));
  CHECK_STATE(2 /*active*/, 0 /*pending*/, concurrent_streams_ - 2 /*connecting capacity*/);
  EXPECT_EQ(2, pool_.host()->cluster().trafficStats().upstream_rq_0rtt_.value());
  EXPECT_EQ(ActiveClient::State::Busy, clients_.back()->state());

  // After 1 stream gets closed, the client should transit to ReadyForEarlyData.
  --clients_.back()->active_streams_;
  pool_.onStreamClosed(*clients_.back(), false);
  EXPECT_EQ(ActiveClient::State::ReadyForEarlyData, clients_.back()->state());
  CHECK_STATE(1 /*active*/, 0 /*pending*/, concurrent_streams_ - 1 /*connecting capacity*/);

  // Creating another early data stream should be immediate.
  EXPECT_CALL(pool_, onPoolReady);
  EXPECT_EQ(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/true));
  EXPECT_EQ(ActiveClient::State::Busy, clients_.back()->state());

  // Even after the client gets connected, it should still be BUSY.
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);
  EXPECT_EQ(ActiveClient::State::Busy, clients_.back()->state());
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 0 /*connecting capacity*/);

  // After 1 stream gets closed, the client should transit to READY.
  --clients_.back()->active_streams_;
  pool_.onStreamClosed(*clients_.back(), false);
  EXPECT_EQ(ActiveClient::State::Ready, clients_.back()->state());

  // Clean up.
  closeStreamAndDrainClient();
}

TEST_F(ConnPoolImplDispatcherBaseTest, PoolDrainsWithEarlyDataStreams) {
  clients_support_early_data_ = true;
  concurrent_streams_ = 2u;
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(2.1));

  EXPECT_CALL(pool_, instantiateActiveClient).Times(2);
  EXPECT_NE(nullptr, pool_.newStreamImpl(context_, /*can_send_early_data=*/true));

  EXPECT_EQ(2u, clients_.size());
  ActiveClient& client_ref = *clients_.back();
  // The first stream should be attached a client upon 0-RTT connected.
  EXPECT_CALL(pool_, onPoolReady);
  client_ref.onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  EXPECT_TRUE(client_ref.readyForStream());
  pool_.onUpstreamReadyForEarlyData(client_ref);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 3 /*connecting capacity*/);

  // Draining existing clients will close the existing CONNECTING client and create a new one.
  EXPECT_CALL(pool_, instantiateActiveClient);
  pool_.drainConnectionsImpl(DrainBehavior::DrainExistingConnections);
  EXPECT_EQ(3u, clients_.size());
  EXPECT_EQ(ActiveClient::State::Draining, client_ref.state());
  // The CONNECTING client should get closed and another new connection should be created.
  EXPECT_EQ(ActiveClient::State::Closed, clients_.front()->state());
  EXPECT_EQ(ActiveClient::State::Connecting, clients_.back()->state());
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 2 /*connecting capacity*/);

  // The 3rd client gets 0-RTT ready.
  clients_.back()->onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  pool_.onUpstreamReadyForEarlyData(*clients_.back());
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 2 /*connecting capacity*/);

  // Drain again to close the 3rd client.
  pool_.drainConnectionsImpl(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  EXPECT_EQ(ActiveClient::State::Closed, clients_.back()->state());
  clients_.pop_back();

  // Clean up.
  closeStream();
}

} // namespace ConnectionPool
} // namespace Envoy
