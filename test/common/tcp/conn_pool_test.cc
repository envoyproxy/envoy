#include <memory>
#include <vector>

#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/tcp/conn_pool.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Tcp {
namespace {

struct TestConnectionState : public ConnectionPool::ConnectionState {
  TestConnectionState(int id, std::function<void()> on_destructor)
      : id_(id), on_destructor_(on_destructor) {}
  ~TestConnectionState() override { on_destructor_(); }

  int id_;
  std::function<void()> on_destructor_;
};

} // namespace

/**
 * Mock callbacks used for conn pool testing.
 */
struct ConnPoolCallbacks : public Tcp::ConnectionPool::Callbacks {
  void onPoolReady(ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override {
    conn_data_ = std::move(conn);
    conn_data_->addUpstreamCallbacks(callbacks_);
    host_ = host;
    ssl_ = conn_data_->connection().streamInfo().downstreamAddressProvider().sslConnection();
    mock_pool_ready_cb_.Call();
  }

  void onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    reason_ = reason;
    host_ = host;
    failure_reason_string_ = std::string(failure_reason);
    mock_pool_failure_cb_.Call();
  }

  StrictMock<ConnectionPool::MockUpstreamCallbacks> callbacks_;
  testing::MockFunction<void()> mock_pool_failure_cb_;
  testing::MockFunction<void()> mock_pool_ready_cb_;
  ConnectionPool::ConnectionDataPtr conn_data_{};
  absl::optional<ConnectionPool::PoolFailureReason> reason_;
  std::string failure_reason_string_;
  Upstream::HostDescriptionConstSharedPtr host_;
  Ssl::ConnectionInfoConstSharedPtr ssl_;
};

class TestActiveTcpClient : public ActiveTcpClient {
public:
  using ActiveTcpClient::ActiveTcpClient;

  ~TestActiveTcpClient() override { parent().onConnDestroyed(); }
  void clearCallbacks() override {
    if (state() == Envoy::ConnectionPool::ActiveClient::State::Busy ||
        state() == Envoy::ConnectionPool::ActiveClient::State::Draining) {
      parent().onConnReleased(*this);
    }
    ActiveTcpClient::clearCallbacks();
  }

  Envoy::Tcp::ConnPoolImpl& parent() { return *static_cast<ConnPoolImpl*>(&parent_); }
};

/**
 * A wrapper around a ConnectionPoolImpl which tracks when the bridge between
 * the pool and the consumer of the connection is released and destroyed.
 */
class ConnPoolBase : public Tcp::ConnectionPool::Instance {
public:
  ConnPoolBase(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
               NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb,
               Network::ConnectionSocket::OptionsSharedPtr options,
               Network::TransportSocketOptionsConstSharedPtr transport_socket_options);

  void addIdleCallback(IdleCb cb) override { conn_pool_->addIdleCallback(cb); }
  bool isIdle() const override { return conn_pool_->isIdle(); }
  void drainConnections(Envoy::ConnectionPool::DrainBehavior drain_behavior) override {
    conn_pool_->drainConnections(drain_behavior);
  }
  void closeConnections() override { conn_pool_->closeConnections(); }
  ConnectionPool::Cancellable* newConnection(Tcp::ConnectionPool::Callbacks& callbacks) override {
    return conn_pool_->newConnection(callbacks);
  }
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

  MOCK_METHOD(void, onConnReleasedForTest, ());
  MOCK_METHOD(void, onConnDestroyedForTest, ());
  bool maybePreconnect(float ratio) override {
    ASSERT(dynamic_cast<ConnPoolImplForTest*>(conn_pool_.get()) != nullptr);
    return dynamic_cast<ConnPoolImplForTest*>(conn_pool_.get())->maybePreconnect(ratio);
  }

  struct TestConnection {
    Network::MockClientConnection* connection_;
    Event::MockTimer* connect_timer_;
    NiceMock<Event::MockTimer>* idle_timer_;
    Network::ReadFilterSharedPtr filter_;
  };

  void expectConnCreate() {
    test_conns_.emplace_back();
    TestConnection& test_conn = test_conns_.back();
    test_conn.connection_ = new NiceMock<Network::MockClientConnection>();
    test_conn.connect_timer_ = new NiceMock<Event::MockTimer>(&mock_dispatcher_);

    Event::MockDispatcher* dispatcher =
        static_cast<Event::MockDispatcher*>(&(test_conn.connection_->dispatcher()));
    if (has_idle_timers_) {
      test_conn.idle_timer_ = new NiceMock<Event::MockTimer>(dispatcher);
    } else {
      // No idle timeout when idle timeout is not configured.
      EXPECT_CALL(*dispatcher, createTimer_(_)).Times(0);
    }

    EXPECT_CALL(mock_dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(test_conn.connection_));
    EXPECT_CALL(*test_conn.connection_, addReadFilter(_))
        .WillOnce(Invoke(
            [&](Network::ReadFilterSharedPtr filter) -> void { test_conn.filter_ = filter; }));
    EXPECT_CALL(*test_conn.connection_, connect());
    EXPECT_CALL(*test_conn.connect_timer_, enableTimer(_, _));

    ON_CALL(*test_conn.connection_, close(Network::ConnectionCloseType::NoFlush))
        .WillByDefault(InvokeWithoutArgs([test_conn]() -> void {
          test_conn.connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
        }));
  }

  void setupIdleTimers() { has_idle_timers_ = true; }

  void expectEnableUpstreamReady(bool run);

  std::unique_ptr<Tcp::ConnectionPool::Instance> conn_pool_;
  Event::MockDispatcher& mock_dispatcher_;
  NiceMock<Event::MockSchedulableCallback>* mock_upstream_ready_cb_;
  std::vector<TestConnection> test_conns_;
  Upstream::HostSharedPtr host_;
  Network::ConnectionCallbacks* callbacks_ = nullptr;
  Network::ConnectionSocket::OptionsSharedPtr options_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  bool has_idle_timers_{false};
  NiceMock<Server::MockOverloadManager> overload_manager_;

protected:
  class ConnPoolImplForTest : public ConnPoolImpl {
  public:
    ConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
                        Network::ConnectionSocket::OptionsSharedPtr options,
                        Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                        ConnPoolBase& parent, Server::OverloadManager& overload_manager)
        : ConnPoolImpl(dispatcher, host, Upstream::ResourcePriority::Default, options,
                       transport_socket_options, state_, absl::nullopt, overload_manager),
          parent_(parent) {}

    void onConnReleased(Envoy::ConnectionPool::ActiveClient& client) override {
      ConnPoolImpl::onConnReleased(client);
      parent_.onConnReleasedForTest();
    }

    Envoy::ConnectionPool::ActiveClientPtr instantiateActiveClient() override {
      absl::optional<std::chrono::milliseconds> idle_timeout;
      if (parent_.has_idle_timers_) {
        // put a random timeout to equip the idle timer, and simulates by manual invokeCallback
        idle_timeout = std::chrono::hours(1);
      }
      return std::make_unique<TestActiveTcpClient>(
          *this, Envoy::ConnectionPool::ConnPoolImplBase::host(), 1, idle_timeout);
    }

    void onConnDestroyed() override { parent_.onConnDestroyedForTest(); }

    Upstream::ClusterConnectivityState state_;
    ConnPoolBase& parent_;
  };
};

ConnPoolBase::ConnPoolBase(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
                           NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb,
                           Network::ConnectionSocket::OptionsSharedPtr options,
                           Network::TransportSocketOptionsConstSharedPtr transport_socket_options)
    : mock_dispatcher_(dispatcher), mock_upstream_ready_cb_(upstream_ready_cb), options_(options),
      transport_socket_options_(transport_socket_options) {
  conn_pool_ = std::make_unique<ConnPoolImplForTest>(
      dispatcher, host, options, transport_socket_options, *this, overload_manager_);
}

void ConnPoolBase::expectEnableUpstreamReady(bool run) {
  if (run) {
    mock_upstream_ready_cb_->invokeCallback();
  } else {
    EXPECT_CALL(*mock_upstream_ready_cb_, scheduleCallbackCurrentIteration())
        .Times(1)
        .RetiresOnSaturation();
  }
}

/**
 * Test fixture for connection pool tests.
 */
class TcpConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  TcpConnPoolImplTest()
      : upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        host_(Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000")) {}

  ~TcpConnPoolImplTest() override {
    EXPECT_TRUE(TestUtility::gaugesZeroed(cluster_->stats_store_.gauges()))
        << TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges());
  }

  void initialize() {
    ON_CALL(*cluster_->upstream_local_address_selector_, getUpstreamLocalAddressImpl(_, _))
        .WillByDefault(
            Return(Upstream::UpstreamLocalAddress({cluster_->source_address_, options_})));
    conn_pool_ = std::make_unique<ConnPoolBase>(dispatcher_, host_, upstream_ready_cb_, options_,
                                                transport_socket_options_);
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  Upstream::HostSharedPtr host_;
  Network::ConnectionSocket::OptionsSharedPtr options_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  std::unique_ptr<ConnPoolBase> conn_pool_;
  NiceMock<Runtime::MockLoader> runtime_;
};

/**
 * Test fixture for connection pool destructor tests.
 */
class TcpConnPoolImplDestructorTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  TcpConnPoolImplDestructorTest()
      : upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)) {
    host_ = Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000");
    conn_pool_ =
        std::make_unique<ConnPoolImpl>(dispatcher_, host_, Upstream::ResourcePriority::Default,
                                       nullptr, nullptr, state_, absl::nullopt, overload_manager_);
    ssl_ = std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>();
  }
  ~TcpConnPoolImplDestructorTest() override = default;

  void prepareConn() {
    connection_ = new StrictMock<Network::MockClientConnection>();
    EXPECT_CALL(*connection_, connectionInfoSetter());
    EXPECT_CALL(*connection_, transportFailureReason()).Times(AtLeast(0));
    EXPECT_CALL(*connection_, setBufferLimits(0));
    EXPECT_CALL(*connection_, detectEarlyCloseWhenReadDisabled(false));
    EXPECT_CALL(*connection_, addConnectionCallbacks(_));
    EXPECT_CALL(*connection_, addReadFilter(_));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, setConnectionStats(_));
    EXPECT_CALL(*connection_, noDelay(true));
    EXPECT_CALL(*connection_, streamInfo()).Times(AnyNumber());
    EXPECT_CALL(*connection_, id()).Times(AnyNumber());
    EXPECT_CALL(*connection_, readDisable(_)).Times(AnyNumber());
    EXPECT_CALL(*connection_, initializeReadFilters());

    connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
    EXPECT_CALL(*connect_timer_, enableTimer(_, _));

    callbacks_ = std::make_unique<ConnPoolCallbacks>();
    ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*callbacks_);
    EXPECT_NE(nullptr, handle);

    EXPECT_CALL(*connect_timer_, disableTimer());
    EXPECT_CALL(callbacks_->mock_pool_ready_cb_, Call);
    connection_->raiseEvent(Network::ConnectionEvent::Connected);
    connection_->stream_info_.downstream_connection_info_provider_->setSslConnection(ssl_);
  }

  Upstream::ClusterConnectivityState state_;
  Upstream::HostConstSharedPtr host_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<NiceMock<Envoy::Ssl::MockConnectionInfo>> ssl_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  NiceMock<Event::MockTimer>* connect_timer_;
  Network::MockClientConnection* connection_;
  std::unique_ptr<Tcp::ConnectionPool::Instance> conn_pool_;
  std::unique_ptr<ConnPoolCallbacks> callbacks_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
};

/**
 * Helper for dealing with an active test connection.
 */
struct ActiveTestConn {
  enum class Type {
    Pending,          // pending request, waiting for free connection
    InProgress,       // connection created, no callback
    CreateConnection, // connection callback occurs after newConnection
    Immediate,        // connection callback occurs during newConnection
  };

  ActiveTestConn(TcpConnPoolImplTest& parent, size_t conn_index, Type type)
      : parent_(parent), conn_index_(conn_index) {
    if (type == Type::CreateConnection || type == Type::InProgress) {
      parent.conn_pool_->expectConnCreate();
    }

    if (type == Type::Immediate) {
      expectNewConn();
    }
    handle_ = parent.conn_pool_->newConnection(callbacks_);

    if (type == Type::Immediate) {
      EXPECT_EQ(nullptr, handle_);
      verifyConn();
    } else {
      EXPECT_NE(nullptr, handle_);
    }

    if (type == Type::CreateConnection) {
      completeConnection();
    }
  }

  void completeConnection() {
    ASSERT_FALSE(completed_);

    EXPECT_CALL(*parent_.conn_pool_->test_conns_[conn_index_].connect_timer_, disableTimer());
    expectNewConn();
    parent_.conn_pool_->test_conns_[conn_index_].connection_->raiseEvent(
        Network::ConnectionEvent::Connected);
    verifyConn();
    completed_ = true;
  }

  void expectNewConn() { EXPECT_CALL(callbacks_.mock_pool_ready_cb_, Call); }

  void releaseConn() { callbacks_.conn_data_.reset(); }

  void verifyConn() {
    EXPECT_EQ(&callbacks_.conn_data_->connection(),
              parent_.conn_pool_->test_conns_[conn_index_].connection_);
  }

  TcpConnPoolImplTest& parent_;
  size_t conn_index_;
  Tcp::ConnectionPool::Cancellable* handle_{};
  ConnPoolCallbacks callbacks_;
  bool completed_{};
};

TEST_F(TcpConnPoolImplTest, Accessors) {
  initialize();
  EXPECT_EQ(conn_pool_->host(), host_);
}

/**
 * Verify that connections are drained when requested.
 */
TEST_F(TcpConnPoolImplTest, DrainConnections) {
  initialize();
  cluster_->resetResourceManager(3, 1024, 1024, 1, 1);

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 1, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c3(*this, 2, ActiveTestConn::Type::InProgress);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  {
    // This will destroy the ready connection and set requests remaining to 1 on the busy and
    // pending connections.
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
    conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
    dispatcher_.clearDeferredDeleteList();
  }
  {
    // This will destroy the busy connection when the response finishes.
    EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
    c2.releaseConn();
    dispatcher_.clearDeferredDeleteList();
  }
  EXPECT_FALSE(conn_pool_->isIdle());
  {
    // This will destroy the pending connection when the response finishes.
    c3.completeConnection();

    EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
    c3.releaseConn();
    dispatcher_.clearDeferredDeleteList();
  }
  EXPECT_TRUE(conn_pool_->isIdle());
}

/**
 * Verify that connections are closed when idle timeout.
 */
TEST_F(TcpConnPoolImplTest, IdleTimerCloseConnections) {
  initialize();
  cluster_->resetResourceManager(1, 1024, 1024, 1, 1);
  conn_pool_->setupIdleTimers();

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  auto idle_timer = conn_pool_->test_conns_[0].idle_timer_;
  // The connection is active.
  EXPECT_FALSE(idle_timer->enabled());

  c1.releaseConn();
  // The connection is idle, which enables idle timer.
  EXPECT_TRUE(idle_timer->enabled());
  {
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());

    auto connection = conn_pool_->test_conns_[0].connection_;
    EXPECT_CALL(*connection, close(Network::ConnectionCloseType::NoFlush))
        .WillOnce(Invoke([&](Network::ConnectionCloseType) -> void {
          connection->raiseEvent(Network::ConnectionEvent::LocalClose);
          // idle timer is disabled.
          EXPECT_FALSE(idle_timer->enabled());
        }));
    idle_timer->invokeCallback();
    dispatcher_.clearDeferredDeleteList();
  }

  // Note that this is pool level idle instead of client/connection level.
  EXPECT_TRUE(conn_pool_->isIdle());

  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_cx_idle_timeout_.value());
}

/**
 * Verify idle timer is disable by remote close.
 */
TEST_F(TcpConnPoolImplTest, ConnectionRemoteCloseDisableIdleTimer) {
  initialize();
  cluster_->resetResourceManager(1, 1024, 1024, 1, 1);
  conn_pool_->setupIdleTimers();

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  auto idle_timer = conn_pool_->test_conns_[0].idle_timer_;
  // The connection is active.
  EXPECT_FALSE(idle_timer->enabled());

  c1.releaseConn();
  // The connection is idle, which enables idle timer.
  EXPECT_TRUE(idle_timer->enabled());
  {
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest()).WillOnce(Invoke([&]() -> void {
      // idle timer is disabled by remote close.
      EXPECT_FALSE(idle_timer->enabled());
    }));
    ;

    conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
    dispatcher_.clearDeferredDeleteList();
  }

  // Note that this is pool level idle instead of client/connection level.
  EXPECT_TRUE(conn_pool_->isIdle());
}

/**
 * Verify idle timer is disable by local close.
 */
TEST_F(TcpConnPoolImplTest, ConnectionLocalCloseDisableIdleTimer) {
  initialize();
  cluster_->resetResourceManager(1, 1024, 1024, 1, 1);
  conn_pool_->setupIdleTimers();

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  auto idle_timer = conn_pool_->test_conns_[0].idle_timer_;
  // The connection is active.
  EXPECT_FALSE(idle_timer->enabled());

  c1.releaseConn();
  // The connection is idle, which enables idle timer.
  EXPECT_TRUE(idle_timer->enabled());
  {
    EXPECT_CALL(*conn_pool_, onConnDestroyedForTest()).WillOnce(Invoke([&]() -> void {
      // idle timer is disabled by local close.
      EXPECT_FALSE(idle_timer->enabled());
    }));
    ;

    conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
    dispatcher_.clearDeferredDeleteList();
  }

  // Note that this is pool level idle instead of client/connection level.
  EXPECT_TRUE(conn_pool_->isIdle());
}

/**
 * Test all timing stats are set.
 */
TEST_F(TcpConnPoolImplTest, VerifyTimingStats) {
  initialize();
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_connect_ms"), _));
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that buffer limits and options are respected.
 */
TEST_F(TcpConnPoolImplTest, VerifyBufferLimitsAndOptions) {
  options_ = std::make_shared<Network::Socket::Options>();
  transport_socket_options_ = std::make_shared<const Network::TransportSocketOptionsImpl>();

  initialize();
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  EXPECT_CALL(*cluster_, perConnectionBufferLimitBytes()).WillOnce(Return(8192));
  EXPECT_CALL(*conn_pool_->test_conns_.back().connection_, setBufferLimits(8192));

  EXPECT_CALL(callbacks.mock_pool_failure_cb_, Call);
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that upstream callback fire for assigned connections.
 */
TEST_F(TcpConnPoolImplTest, UpstreamCallbacks) {
  initialize();
  Buffer::OwnedImpl buffer;

  // Create connection, UpstreamCallbacks set automatically
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  // Expect invocation when connection's ReadFilter::onData is invoked
  EXPECT_CALL(c1.callbacks_.callbacks_, onUpstreamData(_, _));
  EXPECT_EQ(Network::FilterStatus::StopIteration,
            conn_pool_->test_conns_[0].filter_->onData(buffer, false));

  EXPECT_CALL(c1.callbacks_.callbacks_, onAboveWriteBufferHighWatermark());
  for (auto* cb : conn_pool_->test_conns_[0].connection_->callbacks_) {
    cb->onAboveWriteBufferHighWatermark();
  }

  EXPECT_CALL(c1.callbacks_.callbacks_, onBelowWriteBufferLowWatermark());
  for (auto* cb : conn_pool_->test_conns_[0].connection_->callbacks_) {
    cb->onBelowWriteBufferLowWatermark();
  }

  // Shutdown normally.
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests a request that generates a new connection, completes, and then a second request that uses
 * the same connection.
 */
TEST_F(TcpConnPoolImplTest, MultipleRequestAndResponse) {
  initialize();

  // Request 1 should kick off a new connection.
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  // Request 2 should not.
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Immediate);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c2.releaseConn();

  // Cause the connection to go away.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests ConnectionState assignment, lookup and destruction.
 */
TEST_F(TcpConnPoolImplTest, ConnectionStateLifecycle) {
  initialize();

  bool state_destroyed = false;

  // Request 1 should kick off a new connection.
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  auto* state = new TestConnectionState(1, [&]() -> void { state_destroyed = true; });
  c1.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(state));

  EXPECT_EQ(state, c1.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  EXPECT_FALSE(state_destroyed);

  // Request 2 should not.
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Immediate);

  EXPECT_EQ(state, c2.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c2.releaseConn();

  EXPECT_FALSE(state_destroyed);

  // Cause the connection to go away.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_TRUE(state_destroyed);
}

/**
 * Test when we overflow max pending requests.
 */
TEST_F(TcpConnPoolImplTest, MaxPendingRequests) {
  initialize();
  cluster_->resetResourceManager(1, 1, 1024, 1, 1);

  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  ConnPoolCallbacks callbacks2;
  EXPECT_CALL(callbacks2.mock_pool_failure_cb_, Call);
  Tcp::ConnectionPool::Cancellable* handle2 = conn_pool_->newConnection(callbacks2);
  EXPECT_EQ(nullptr, handle2);

  handle->cancel(ConnectionPool::CancelPolicy::Default);

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::Overflow, callbacks2.reason_);

  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_rq_pending_overflow_.value());
}

/**
 * Tests a connection failure before a request is bound which should result in the pending request
 * getting purged.
 */
TEST_F(TcpConnPoolImplTest, RemoteConnectFailure) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.mock_pool_failure_cb_, Call);
  EXPECT_CALL(*conn_pool_->test_conns_[0].connect_timer_, disableTimer());

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  EXPECT_CALL(*conn_pool_->test_conns_[0].connection_, transportFailureReason())
      .WillOnce(Return("foo"));
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, callbacks.reason_);
  EXPECT_EQ("foo", callbacks.failure_reason_string_);

  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_rq_pending_failure_eject_.value());
}

/**
 * Tests a connection failure before a request is bound which should result in the pending request
 * getting purged.
 */
TEST_F(TcpConnPoolImplTest, LocalConnectFailure) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.mock_pool_failure_cb_, Call);
  EXPECT_CALL(*conn_pool_->test_conns_[0].connect_timer_, disableTimer());

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::LocalConnectionFailure, callbacks.reason_);

  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_rq_pending_failure_eject_.value());
}

/**
 * Tests a connect timeout. Also test that we can add a new request during ejection processing.
 */
TEST_F(TcpConnPoolImplTest, ConnectTimeout) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks1;
  conn_pool_->expectConnCreate();
  EXPECT_NE(nullptr, conn_pool_->newConnection(callbacks1));

  ConnPoolCallbacks callbacks2;
  EXPECT_CALL(callbacks1.mock_pool_failure_cb_, Call).WillOnce([&]() -> void {
    conn_pool_->expectConnCreate();
    EXPECT_NE(nullptr, conn_pool_->newConnection(callbacks2));
  });

  conn_pool_->test_conns_[0].connect_timer_->invokeCallback();

  EXPECT_CALL(callbacks2.mock_pool_failure_cb_, Call);
  conn_pool_->test_conns_[1].connect_timer_->invokeCallback();

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::Timeout, callbacks1.reason_);
  EXPECT_EQ(ConnectionPool::PoolFailureReason::Timeout, callbacks2.reason_);

  EXPECT_EQ(2U, cluster_->traffic_stats_->upstream_cx_connect_fail_.value());
  EXPECT_EQ(2U, cluster_->traffic_stats_->upstream_cx_connect_timeout_.value());
}

/**
 * Test cancelling before the request is bound to a connection.
 */
TEST_F(TcpConnPoolImplTest, CancelBeforeBound) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  handle->cancel(ConnectionPool::CancelPolicy::Default);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Cause the connection to go away.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test cancelling before the request is bound to a connection, with connection close.
 */
TEST_F(TcpConnPoolImplTest, CancelAndCloseBeforeBound) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  // Expect the connection is closed.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  handle->cancel(ConnectionPool::CancelPolicy::CloseExcess);

  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test an upstream disconnection while there is a bound request.
 */
TEST_F(TcpConnPoolImplTest, DisconnectWhileBound) {
  initialize();

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.mock_pool_ready_cb_, Call);

  EXPECT_CALL(callbacks.callbacks_, onEvent(_));
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Kill the connection while it has an active request.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test upstream disconnection of one request while another is pending.
 */
TEST_F(TcpConnPoolImplTest, DisconnectWhilePending) {
  initialize();
  cluster_->resetResourceManager(1, 1024, 1024, 1, 1);

  // First request connected.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(*conn_pool_->test_conns_[0].connect_timer_, disableTimer());
  EXPECT_CALL(callbacks.mock_pool_ready_cb_, Call);
  EXPECT_CALL(callbacks.callbacks_, onEvent(_));
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Second request pending.
  ConnPoolCallbacks callbacks2;
  ConnectionPool::Cancellable* handle2 = conn_pool_->newConnection(callbacks2);
  EXPECT_NE(nullptr, handle2);

  // Connection closed, triggering new connection for pending request.
  conn_pool_->expectConnCreate();
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();

  // test_conns_[1] is the new connection
  EXPECT_CALL(*conn_pool_->test_conns_[1].connect_timer_, disableTimer());
  EXPECT_CALL(callbacks2.mock_pool_ready_cb_, Call);
  conn_pool_->test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  callbacks2.conn_data_.reset();

  // Disconnect
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that we correctly handle reaching max connections.
 */
TEST_F(TcpConnPoolImplTest, MaxConnections) {
  initialize();
  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);

  EXPECT_NE(nullptr, handle);

  // Request 2 should not kick off a new connection.
  ConnPoolCallbacks callbacks2;
  handle = conn_pool_->newConnection(callbacks2);
  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_cx_overflow_.value());

  EXPECT_NE(nullptr, handle);

  // Connect event will bind to request 1.
  EXPECT_CALL(callbacks.mock_pool_ready_cb_, Call);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Finishing request 1 will immediately bind to request 2.
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  conn_pool_->expectEnableUpstreamReady(false);
  EXPECT_CALL(callbacks2.mock_pool_ready_cb_, Call);
  callbacks.conn_data_.reset();

  conn_pool_->expectEnableUpstreamReady(true);
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  callbacks2.conn_data_.reset();

  // Cause the connection to go away.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test when we reach max requests per connection.
 */
TEST_F(TcpConnPoolImplTest, MaxRequestsPerConnection) {
  initialize();

  cluster_->max_requests_per_connection_ = 1;

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);

  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.mock_pool_ready_cb_, Call);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  callbacks.conn_data_.reset();
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, cluster_->traffic_stats_->upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1U, cluster_->traffic_stats_->upstream_cx_max_requests_.value());
}

/*
 * Test that multiple connections can be assigned at once.
 */
TEST_F(TcpConnPoolImplTest, ConcurrentConnections) {
  initialize();
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 1, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c3(*this, 0, ActiveTestConn::Type::Pending);

  // Finish c1, which gets c3 going.
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  conn_pool_->expectEnableUpstreamReady(false);
  c3.expectNewConn();
  c1.releaseConn();

  conn_pool_->expectEnableUpstreamReady(true);
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest()).Times(2);
  c2.releaseConn();
  c3.releaseConn();

  // Disconnect both connections.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest()).Times(2);
  conn_pool_->test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests ConnectionState lifecycle with multiple concurrent connections.
 */
TEST_F(TcpConnPoolImplTest, ConnectionStateWithConcurrentConnections) {
  initialize();

  int state_destroyed = 0;
  auto* s1 = new TestConnectionState(1, [&]() -> void { state_destroyed |= 1; });
  auto* s2 = new TestConnectionState(2, [&]() -> void { state_destroyed |= 2; });
  auto* s3 = new TestConnectionState(2, [&]() -> void { state_destroyed |= 4; });

  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  c1.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(s1));
  ActiveTestConn c2(*this, 1, ActiveTestConn::Type::CreateConnection);
  c2.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(s2));
  ActiveTestConn c3(*this, 0, ActiveTestConn::Type::Pending);

  EXPECT_EQ(0, state_destroyed);

  // Finish c1, which gets c3 going.
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  conn_pool_->expectEnableUpstreamReady(false);
  c3.expectNewConn();
  c1.releaseConn();

  conn_pool_->expectEnableUpstreamReady(true);

  // c3 now has the state set by c1.
  EXPECT_EQ(s1, c3.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());
  EXPECT_EQ(s2, c2.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  // replace c3's state
  c3.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(s3));
  EXPECT_EQ(1, state_destroyed);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest()).Times(2);
  c2.releaseConn();
  c3.releaseConn();

  EXPECT_EQ(1, state_destroyed);

  // Disconnect both connections.
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest()).Times(2);
  conn_pool_->test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(7, state_destroyed);
}

/**
 * Tests that the DrainCallback is invoked when the number of connections goes to zero.
 */
TEST_F(TcpConnPoolImplTest, DrainCallback) {
  initialize();
  ReadyWatcher drained;
  conn_pool_->addIdleCallback([&]() -> void { drained.ready(); });

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Pending);
  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  c2.handle_->cancel(ConnectionPool::CancelPolicy::Default);

  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  EXPECT_CALL(drained, ready()).Times(AtLeast(1));
  c1.releaseConn();

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test draining a connection pool that has a pending connection.
 */
TEST_F(TcpConnPoolImplTest, DrainWhileConnecting) {
  initialize();
  ReadyWatcher drained;

  ConnPoolCallbacks callbacks;
  conn_pool_->expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  conn_pool_->addIdleCallback([&]() -> void { drained.ready(); });
  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  // The shared connection pool removes and closes connecting clients if there are no
  // pending requests.
  EXPECT_CALL(drained, ready()).Times(AtLeast(1));
  handle->cancel(ConnectionPool::CancelPolicy::Default);
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that the DrainCallback is invoked when a connection is closed.
 */
TEST_F(TcpConnPoolImplTest, DrainOnClose) {
  initialize();
  ReadyWatcher drained;
  conn_pool_->addIdleCallback([&]() -> void { drained.ready(); });
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  EXPECT_CALL(drained, ready()).Times(AtLeast(1));
  EXPECT_CALL(c1.callbacks_.callbacks_, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent event) -> void {
        EXPECT_EQ(Network::ConnectionEvent::RemoteClose, event);
        c1.releaseConn();
      }));
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test connecting_request_capacity logic.
 */
TEST_F(TcpConnPoolImplTest, RequestCapacity) {
  initialize();
  cluster_->resetResourceManager(5, 1024, 1024, 1, 1);
  cluster_->max_requests_per_connection_ = 100;

  ConnPoolCallbacks callbacks1;
  ConnPoolCallbacks callbacks2;
  Tcp::ConnectionPool::Cancellable* handle1;
  Tcp::ConnectionPool::Cancellable* handle2;
  {
    // Request 1 should kick off a new connection.
    conn_pool_->expectConnCreate();
    handle1 = conn_pool_->newConnection(callbacks1);
    EXPECT_NE(nullptr, handle1);
  }
  {
    // Request 2 should kick off a new connection.
    conn_pool_->expectConnCreate();
    handle2 = conn_pool_->newConnection(callbacks2);
    EXPECT_NE(nullptr, handle2);
  }

  // This should set the number of requests remaining to 1 on the active
  // connections, and the connecting_request_capacity to 2 as well.
  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

  // Cancel the connections. Because neither used CloseExcess, the two connections should persist.
  handle1->cancel(ConnectionPool::CancelPolicy::Default);
  handle2->cancel(ConnectionPool::CancelPolicy::Default);

  Tcp::ConnectionPool::Cancellable* handle3;
  Tcp::ConnectionPool::Cancellable* handle4;
  Tcp::ConnectionPool::Cancellable* handle5;
  ConnPoolCallbacks callbacks3;
  ConnPoolCallbacks callbacks4;
  ConnPoolCallbacks callbacks5;

  {
    // The next two requests will use the connections in progress, bringing
    // connecting_request_capacity to zero.
    handle3 = conn_pool_->newConnection(callbacks3);
    EXPECT_NE(nullptr, handle3);

    handle4 = conn_pool_->newConnection(callbacks4);
    EXPECT_NE(nullptr, handle4);
  }
  {
    // With connecting_request_capacity zero, a request for a new connection
    // will kick off connection #3.
    conn_pool_->expectConnCreate();
    handle5 = conn_pool_->newConnection(callbacks5);
    EXPECT_NE(nullptr, handle5);
  }

  // Clean up remaining connections.
  handle3->cancel(ConnectionPool::CancelPolicy::Default);
  handle4->cancel(ConnectionPool::CancelPolicy::Default);
  handle5->cancel(ConnectionPool::CancelPolicy::Default);
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_->test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_->test_conns_[2].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that connections that are closed due to idle timeout causes the idle callback to be fired.
TEST_F(TcpConnPoolImplTest, TestIdleTimeout) {
  initialize();
  testing::MockFunction<void()> idle_callback;
  conn_pool_->addIdleCallback(idle_callback.AsStdFunction());

  EXPECT_CALL(idle_callback, Call());
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  EXPECT_CALL(*conn_pool_, onConnReleasedForTest());
  c1.releaseConn();
  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  conn_pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  EXPECT_CALL(*conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

// Test that maybePreconnect is passed up to the base class implementation.
TEST_F(TcpConnPoolImplTest, TestPreconnect) {
  initialize();
  EXPECT_FALSE(conn_pool_->maybePreconnect(0));

  conn_pool_->expectConnCreate();
  ASSERT_TRUE(conn_pool_->maybePreconnect(2));

  conn_pool_->test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

/**
 * Test that pending connections are closed when the connection pool is destroyed.
 */
TEST_F(TcpConnPoolImplDestructorTest, TestPendingConnectionsAreClosed) {
  connection_ = new NiceMock<Network::MockClientConnection>();
  connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
  EXPECT_CALL(*connect_timer_, enableTimer(_, _));

  callbacks_ = std::make_unique<ConnPoolCallbacks>();
  ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*callbacks_);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks_->mock_pool_failure_cb_, Call);
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}

/**
 * Test that busy connections are closed when the connection pool is destroyed.
 */
TEST_F(TcpConnPoolImplDestructorTest, TestBusyConnectionsAreClosed) {
  prepareConn();

  EXPECT_CALL(callbacks_->callbacks_, onEvent(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}

/**
 * Test that ready connections are closed when the connection pool is destroyed.
 */
TEST_F(TcpConnPoolImplDestructorTest, TestReadyConnectionsAreClosed) {
  prepareConn();

  // Transition connection to ready list
  callbacks_->conn_data_.reset();

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}

} // namespace Tcp
} // namespace Envoy
