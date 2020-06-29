#include <memory>
#include <vector>

#include "common/event/dispatcher_impl.h"
#include "common/network/utility.h"
#include "common/tcp/conn_pool.h"
#include "common/tcp/original_conn_pool.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Property;
using testing::Return;

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
    host_ = host;
    pool_ready_.ready();
  }

  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    reason_ = reason;
    host_ = host;
    pool_failure_.ready();
  }

  ReadyWatcher pool_failure_;
  ReadyWatcher pool_ready_;
  ConnectionPool::ConnectionDataPtr conn_data_{};
  absl::optional<ConnectionPool::PoolFailureReason> reason_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

/**
 * A wrapper around a ConnectionPoolImpl which tracks when the bridge between
 * the pool and the consumer of the connection is released and destroyed.
 */
class ConnPoolBase : public Tcp::ConnectionPool::Instance {
public:
  ConnPoolBase(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
               NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb,
               bool test_new_connection_pool);

  void addDrainedCallback(DrainedCb cb) override { conn_pool_->addDrainedCallback(cb); }
  void drainConnections() override { conn_pool_->drainConnections(); }
  void closeConnections() override { conn_pool_->closeConnections(); }
  ConnectionPool::Cancellable* newConnection(Tcp::ConnectionPool::Callbacks& callbacks) override {
    return conn_pool_->newConnection(callbacks);
  }
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

  MOCK_METHOD(void, onConnReleasedForTest, ());
  MOCK_METHOD(void, onConnDestroyedForTest, ());

  struct TestConnection {
    Network::MockClientConnection* connection_;
    Event::MockTimer* connect_timer_;
    Network::ReadFilterSharedPtr filter_;
  };

  void expectConnCreate() {
    test_conns_.emplace_back();
    TestConnection& test_conn = test_conns_.back();
    test_conn.connection_ = new NiceMock<Network::MockClientConnection>();
    test_conn.connect_timer_ = new NiceMock<Event::MockTimer>(&mock_dispatcher_);

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

  void expectEnableUpstreamReady(bool run);

  std::unique_ptr<Tcp::ConnectionPool::Instance> conn_pool_;
  Event::MockDispatcher& mock_dispatcher_;
  NiceMock<Event::MockSchedulableCallback>* mock_upstream_ready_cb_;
  std::vector<TestConnection> test_conns_;
  Network::ConnectionCallbacks* callbacks_ = nullptr;
  bool test_new_connection_pool_;

protected:
  class ConnPoolImplForTest : public ConnPoolImpl {
  public:
    ConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
                        ConnPoolBase& parent)
        : ConnPoolImpl(dispatcher, host, Upstream::ResourcePriority::Default, nullptr, nullptr),
          parent_(parent) {}

    void onConnReleased(Envoy::ConnectionPool::ActiveClient& client) override {
      ConnPoolImpl::onConnReleased(client);
      parent_.onConnReleasedForTest();
    }

    void onConnDestroyed() override { parent_.onConnDestroyedForTest(); }
    ConnPoolBase& parent_;
  };

  class OriginalConnPoolImplForTest : public OriginalConnPoolImpl {
  public:
    OriginalConnPoolImplForTest(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
                                ConnPoolBase& parent)
        : OriginalConnPoolImpl(dispatcher, host, Upstream::ResourcePriority::Default, nullptr,
                               nullptr),
          parent_(parent) {}

    ~OriginalConnPoolImplForTest() override {
      EXPECT_EQ(0U, ready_conns_.size());
      EXPECT_EQ(0U, busy_conns_.size());
      EXPECT_EQ(0U, pending_requests_.size());
    }

    void onConnReleased(OriginalConnPoolImpl::ActiveConn& conn) override {
      parent_.onConnReleasedForTest();
      OriginalConnPoolImpl::onConnReleased(conn);
    }

    void onConnDestroyed(OriginalConnPoolImpl::ActiveConn& conn) override {
      parent_.onConnDestroyedForTest();
      OriginalConnPoolImpl::onConnDestroyed(conn);
    }
    void expectEnableUpstreamReady(bool run) {
      if (!run) {
        EXPECT_FALSE(upstream_ready_enabled_);
        EXPECT_CALL(*parent_.mock_upstream_ready_cb_, scheduleCallbackCurrentIteration())
            .Times(1)
            .RetiresOnSaturation();
      } else {
        EXPECT_TRUE(upstream_ready_enabled_);
        parent_.mock_upstream_ready_cb_->invokeCallback();
        EXPECT_FALSE(upstream_ready_enabled_);
      }
    }
    ConnPoolBase& parent_;
  };
};

ConnPoolBase::ConnPoolBase(Event::MockDispatcher& dispatcher, Upstream::HostSharedPtr host,
                           NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb,
                           bool test_new_connection_pool)
    : mock_dispatcher_(dispatcher), mock_upstream_ready_cb_(upstream_ready_cb),
      test_new_connection_pool_(test_new_connection_pool) {
  if (test_new_connection_pool_) {
    conn_pool_ = std::make_unique<ConnPoolImplForTest>(dispatcher, host, *this);
  } else {
    conn_pool_ = std::make_unique<OriginalConnPoolImplForTest>(dispatcher, host, *this);
  }
}

void ConnPoolBase::expectEnableUpstreamReady(bool run) {
  if (!test_new_connection_pool_) {
    dynamic_cast<OriginalConnPoolImplForTest*>(conn_pool_.get())->expectEnableUpstreamReady(run);
  } else {
    if (!run) {
      EXPECT_CALL(*mock_upstream_ready_cb_, scheduleCallbackCurrentIteration())
          .Times(1)
          .RetiresOnSaturation();
    } else {
      mock_upstream_ready_cb_->invokeCallback();
    }
  }
}

/**
 * Test fixture for connection pool tests.
 */
class TcpConnPoolImplTest : public testing::TestWithParam<bool> {
public:
  TcpConnPoolImplTest()
      : test_new_connection_pool_(GetParam()),
        upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        host_(Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000")),
        conn_pool_(dispatcher_, host_, upstream_ready_cb_, test_new_connection_pool_) {}

  ~TcpConnPoolImplTest() override {
    EXPECT_TRUE(TestUtility::gaugesZeroed(cluster_->stats_store_.gauges()))
        << TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges());
  }

  bool test_new_connection_pool_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  Upstream::HostSharedPtr host_;
  ConnPoolBase conn_pool_;
  NiceMock<Runtime::MockLoader> runtime_;
};

/**
 * Test fixture for connection pool destructor tests.
 */
class TcpConnPoolImplDestructorTest : public testing::TestWithParam<bool> {
public:
  TcpConnPoolImplDestructorTest()
      : test_new_connection_pool_(GetParam()),
        upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)) {
    host_ = Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000");
    if (test_new_connection_pool_) {
      conn_pool_ = std::make_unique<ConnPoolImpl>(
          dispatcher_, host_, Upstream::ResourcePriority::Default, nullptr, nullptr);
    } else {
      conn_pool_ = std::make_unique<OriginalConnPoolImpl>(
          dispatcher_, host_, Upstream::ResourcePriority::Default, nullptr, nullptr);
    }
  }
  ~TcpConnPoolImplDestructorTest() override = default;

  void prepareConn() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
    EXPECT_CALL(*connect_timer_, enableTimer(_, _));

    callbacks_ = std::make_unique<ConnPoolCallbacks>();
    ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*callbacks_);
    EXPECT_NE(nullptr, handle);

    EXPECT_CALL(*connect_timer_, disableTimer());
    EXPECT_CALL(callbacks_->pool_ready_, ready());
    connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  bool test_new_connection_pool_;
  Upstream::HostConstSharedPtr host_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  NiceMock<Event::MockTimer>* connect_timer_;
  NiceMock<Network::MockClientConnection>* connection_;
  std::unique_ptr<Tcp::ConnectionPool::Instance> conn_pool_;
  std::unique_ptr<ConnPoolCallbacks> callbacks_;
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
      parent.conn_pool_.expectConnCreate();
    }

    if (type == Type::Immediate) {
      expectNewConn();
    }
    handle_ = parent.conn_pool_.newConnection(callbacks_);

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

    EXPECT_CALL(*parent_.conn_pool_.test_conns_[conn_index_].connect_timer_, disableTimer());
    expectNewConn();
    parent_.conn_pool_.test_conns_[conn_index_].connection_->raiseEvent(
        Network::ConnectionEvent::Connected);
    verifyConn();
    completed_ = true;
  }

  void expectNewConn() { EXPECT_CALL(callbacks_.pool_ready_, ready()); }

  void releaseConn() { callbacks_.conn_data_.reset(); }

  void verifyConn() {
    EXPECT_EQ(&callbacks_.conn_data_->connection(),
              parent_.conn_pool_.test_conns_[conn_index_].connection_);
  }

  TcpConnPoolImplTest& parent_;
  size_t conn_index_;
  Tcp::ConnectionPool::Cancellable* handle_{};
  ConnPoolCallbacks callbacks_;
  bool completed_{};
};

TEST_P(TcpConnPoolImplTest, HostAccessor) { EXPECT_EQ(conn_pool_.host(), host_); }

/**
 * Verify that connections are drained when requested.
 */
TEST_P(TcpConnPoolImplTest, DrainConnections) {
  cluster_->resetResourceManager(3, 1024, 1024, 1, 1);

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 1, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c3(*this, 2, ActiveTestConn::Type::InProgress);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  {
    // This will destroy the ready connection and set requests remaining to 1 on the busy and
    // pending connections.
    EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
    conn_pool_.drainConnections();
    dispatcher_.clearDeferredDeleteList();
  }
  {
    // This will destroy the busy connection when the response finishes.
    EXPECT_CALL(conn_pool_, onConnReleasedForTest());
    EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
    c2.releaseConn();
    dispatcher_.clearDeferredDeleteList();
  }
  {
    // This will destroy the pending connection when the response finishes.
    c3.completeConnection();

    EXPECT_CALL(conn_pool_, onConnReleasedForTest());
    EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
    c3.releaseConn();
    dispatcher_.clearDeferredDeleteList();
  }
}

/**
 * Test all timing stats are set.
 */
TEST_P(TcpConnPoolImplTest, VerifyTimingStats) {
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_connect_ms"), _));
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that buffer limits are set.
 */
TEST_P(TcpConnPoolImplTest, VerifyBufferLimits) {
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  EXPECT_CALL(*cluster_, perConnectionBufferLimitBytes()).WillOnce(Return(8192));
  EXPECT_CALL(*conn_pool_.test_conns_.back().connection_, setBufferLimits(8192));

  EXPECT_CALL(callbacks.pool_failure_, ready());
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that upstream callback fire for assigned connections.
 */
TEST_P(TcpConnPoolImplTest, UpstreamCallbacks) {
  Buffer::OwnedImpl buffer;

  ConnectionPool::MockUpstreamCallbacks callbacks;

  // Create connection, set UpstreamCallbacks
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  c1.callbacks_.conn_data_->addUpstreamCallbacks(callbacks);

  // Expect invocation when connection's ReadFilter::onData is invoked
  EXPECT_CALL(callbacks, onUpstreamData(_, _));
  EXPECT_EQ(Network::FilterStatus::StopIteration,
            conn_pool_.test_conns_[0].filter_->onData(buffer, false));

  EXPECT_CALL(callbacks, onAboveWriteBufferHighWatermark());
  for (auto* cb : conn_pool_.test_conns_[0].connection_->callbacks_) {
    cb->onAboveWriteBufferHighWatermark();
  }

  EXPECT_CALL(callbacks, onBelowWriteBufferLowWatermark());
  for (auto* cb : conn_pool_.test_conns_[0].connection_->callbacks_) {
    cb->onBelowWriteBufferLowWatermark();
  }

  // Shutdown normally.
  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that upstream callback close event fires for assigned connections.
 */
TEST_P(TcpConnPoolImplTest, UpstreamCallbacksCloseEvent) {
  Buffer::OwnedImpl buffer;

  ConnectionPool::MockUpstreamCallbacks callbacks;

  // Create connection, set UpstreamCallbacks
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  c1.callbacks_.conn_data_->addUpstreamCallbacks(callbacks);

  EXPECT_CALL(callbacks, onEvent(Network::ConnectionEvent::RemoteClose));

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that a connection pool functions without upstream callbacks.
 */
TEST_P(TcpConnPoolImplTest, NoUpstreamCallbacks) {
  Buffer::OwnedImpl buffer;

  // Create connection.
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  // Trigger connection's ReadFilter::onData -- connection pool closes connection.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  EXPECT_EQ(Network::FilterStatus::StopIteration,
            conn_pool_.test_conns_[0].filter_->onData(buffer, false));
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests a request that generates a new connection, completes, and then a second request that uses
 * the same connection.
 */
TEST_P(TcpConnPoolImplTest, MultipleRequestAndResponse) {

  // Request 1 should kick off a new connection.
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  // Request 2 should not.
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Immediate);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c2.releaseConn();

  // Cause the connection to go away.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests ConnectionState assignment, lookup and destruction.
 */
TEST_P(TcpConnPoolImplTest, ConnectionStateLifecycle) {

  bool state_destroyed = false;

  // Request 1 should kick off a new connection.
  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  auto* state = new TestConnectionState(1, [&]() -> void { state_destroyed = true; });
  c1.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(state));

  EXPECT_EQ(state, c1.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c1.releaseConn();

  EXPECT_FALSE(state_destroyed);

  // Request 2 should not.
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Immediate);

  EXPECT_EQ(state, c2.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  c2.releaseConn();

  EXPECT_FALSE(state_destroyed);

  // Cause the connection to go away.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_TRUE(state_destroyed);
}

/**
 * Test when we overflow max pending requests.
 */
TEST_P(TcpConnPoolImplTest, MaxPendingRequests) {
  cluster_->resetResourceManager(1, 1, 1024, 1, 1);

  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  ConnPoolCallbacks callbacks2;
  EXPECT_CALL(callbacks2.pool_failure_, ready());
  Tcp::ConnectionPool::Cancellable* handle2 = conn_pool_.newConnection(callbacks2);
  EXPECT_EQ(nullptr, handle2);

  handle->cancel(ConnectionPool::CancelPolicy::Default);

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::Overflow, callbacks2.reason_);

  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_pending_overflow_.value());
}

/**
 * Tests a connection failure before a request is bound which should result in the pending request
 * getting purged.
 */
TEST_P(TcpConnPoolImplTest, RemoteConnectFailure) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_CALL(*conn_pool_.test_conns_[0].connect_timer_, disableTimer());

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, callbacks.reason_);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_pending_failure_eject_.value());
}

/**
 * Tests a connection failure before a request is bound which should result in the pending request
 * getting purged.
 */
TEST_P(TcpConnPoolImplTest, LocalConnectFailure) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_CALL(*conn_pool_.test_conns_[0].connect_timer_, disableTimer());

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::LocalConnectionFailure, callbacks.reason_);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_pending_failure_eject_.value());
}

/**
 * Tests a connect timeout. Also test that we can add a new request during ejection processing.
 */
TEST_P(TcpConnPoolImplTest, ConnectTimeout) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks1;
  conn_pool_.expectConnCreate();
  EXPECT_NE(nullptr, conn_pool_.newConnection(callbacks1));

  ConnPoolCallbacks callbacks2;
  EXPECT_CALL(callbacks1.pool_failure_, ready()).WillOnce(Invoke([&]() -> void {
    conn_pool_.expectConnCreate();
    EXPECT_NE(nullptr, conn_pool_.newConnection(callbacks2));
  }));

  conn_pool_.test_conns_[0].connect_timer_->invokeCallback();

  EXPECT_CALL(callbacks2.pool_failure_, ready());
  conn_pool_.test_conns_[1].connect_timer_->invokeCallback();

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(ConnectionPool::PoolFailureReason::Timeout, callbacks1.reason_);
  EXPECT_EQ(ConnectionPool::PoolFailureReason::Timeout, callbacks2.reason_);

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_connect_timeout_.value());
}

/**
 * Test cancelling before the request is bound to a connection.
 */
TEST_P(TcpConnPoolImplTest, CancelBeforeBound) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  handle->cancel(ConnectionPool::CancelPolicy::Default);
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Cause the connection to go away.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test cancelling before the request is bound to a connection, with connection close.
 */
TEST_P(TcpConnPoolImplTest, CancelAndCloseBeforeBound) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  // Expect the connection is closed.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  handle->cancel(ConnectionPool::CancelPolicy::CloseExcess);

  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test an upstream disconnection while there is a bound request.
 */
TEST_P(TcpConnPoolImplTest, DisconnectWhileBound) {

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.pool_ready_, ready());

  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Kill the connection while it has an active request.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test upstream disconnection of one request while another is pending.
 */
TEST_P(TcpConnPoolImplTest, DisconnectWhilePending) {
  cluster_->resetResourceManager(1, 1024, 1024, 1, 1);

  // First request connected.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(*conn_pool_.test_conns_[0].connect_timer_, disableTimer());
  EXPECT_CALL(callbacks.pool_ready_, ready());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Second request pending.
  ConnPoolCallbacks callbacks2;
  ConnectionPool::Cancellable* handle2 = conn_pool_.newConnection(callbacks2);
  EXPECT_NE(nullptr, handle2);

  // Connection closed, triggering new connection for pending request.
  conn_pool_.expectConnCreate();
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();

  // test_conns_[1] is the new connection
  EXPECT_CALL(*conn_pool_.test_conns_[1].connect_timer_, disableTimer());
  EXPECT_CALL(callbacks2.pool_ready_, ready());
  conn_pool_.test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  callbacks2.conn_data_.reset();

  // Disconnect
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that we correctly handle reaching max connections.
 */
TEST_P(TcpConnPoolImplTest, MaxConnections) {
  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);

  EXPECT_NE(nullptr, handle);

  // Request 2 should not kick off a new connection.
  ConnPoolCallbacks callbacks2;
  handle = conn_pool_.newConnection(callbacks2);
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_overflow_.value());

  EXPECT_NE(nullptr, handle);

  // Connect event will bind to request 1.
  EXPECT_CALL(callbacks.pool_ready_, ready());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Finishing request 1 will immediately bind to request 2.
  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  conn_pool_.expectEnableUpstreamReady(false);
  EXPECT_CALL(callbacks2.pool_ready_, ready());
  callbacks.conn_data_.reset();

  conn_pool_.expectEnableUpstreamReady(true);
  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  callbacks2.conn_data_.reset();

  // Cause the connection to go away.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test when we reach max requests per connection.
 */
TEST_P(TcpConnPoolImplTest, MaxRequestsPerConnection) {

  cluster_->max_requests_per_connection_ = 1;

  // Request 1 should kick off a new connection.
  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);

  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks.pool_ready_, ready());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  callbacks.conn_data_.reset();
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_max_requests_.value());
}

/*
 * Test that multiple connections can be assigned at once.
 */
TEST_P(TcpConnPoolImplTest, ConcurrentConnections) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 1, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c3(*this, 0, ActiveTestConn::Type::Pending);

  // Finish c1, which gets c3 going.
  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  conn_pool_.expectEnableUpstreamReady(false);
  c3.expectNewConn();
  c1.releaseConn();

  conn_pool_.expectEnableUpstreamReady(true);
  EXPECT_CALL(conn_pool_, onConnReleasedForTest()).Times(2);
  c2.releaseConn();
  c3.releaseConn();

  // Disconnect both connections.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest()).Times(2);
  conn_pool_.test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Tests ConnectionState lifecycle with multiple concurrent connections.
 */
TEST_P(TcpConnPoolImplTest, ConnectionStateWithConcurrentConnections) {

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
  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  conn_pool_.expectEnableUpstreamReady(false);
  c3.expectNewConn();
  c1.releaseConn();

  conn_pool_.expectEnableUpstreamReady(true);

  // c3 now has the state set by c1.
  EXPECT_EQ(s1, c3.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());
  EXPECT_EQ(s2, c2.callbacks_.conn_data_->connectionStateTyped<TestConnectionState>());

  // replace c3's state
  c3.callbacks_.conn_data_->setConnectionState(std::unique_ptr<TestConnectionState>(s3));
  EXPECT_EQ(1, state_destroyed);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest()).Times(2);
  c2.releaseConn();
  c3.releaseConn();

  EXPECT_EQ(1, state_destroyed);

  // Disconnect both connections.
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest()).Times(2);
  conn_pool_.test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(7, state_destroyed);
}

/**
 * Tests that the DrainCallback is invoked when the number of connections goes to zero.
 */
TEST_P(TcpConnPoolImplTest, DrainCallback) {
  ReadyWatcher drained;

  EXPECT_CALL(drained, ready());
  conn_pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);
  ActiveTestConn c2(*this, 0, ActiveTestConn::Type::Pending);
  c2.handle_->cancel(ConnectionPool::CancelPolicy::Default);

  EXPECT_CALL(conn_pool_, onConnReleasedForTest());
  EXPECT_CALL(drained, ready());
  c1.releaseConn();

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test draining a connection pool that has a pending connection.
 */
TEST_P(TcpConnPoolImplTest, DrainWhileConnecting) {
  ReadyWatcher drained;

  ConnPoolCallbacks callbacks;
  conn_pool_.expectConnCreate();
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(callbacks);
  EXPECT_NE(nullptr, handle);

  conn_pool_.addDrainedCallback([&]() -> void { drained.ready(); });
  if (test_new_connection_pool_) {
    // The shared connection pool removes and closes connecting clients if there are no
    // pending requests.
    EXPECT_CALL(drained, ready());
    handle->cancel(ConnectionPool::CancelPolicy::Default);
  } else {
    handle->cancel(ConnectionPool::CancelPolicy::Default);
    EXPECT_CALL(*conn_pool_.test_conns_[0].connection_,
                close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(drained, ready());
    conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }
  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that the DrainCallback is invoked when a connection is closed.
 */
TEST_P(TcpConnPoolImplTest, DrainOnClose) {
  ReadyWatcher drained;
  EXPECT_CALL(drained, ready());
  conn_pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  ActiveTestConn c1(*this, 0, ActiveTestConn::Type::CreateConnection);

  ConnectionPool::MockUpstreamCallbacks callbacks;
  c1.callbacks_.conn_data_->addUpstreamCallbacks(callbacks);

  EXPECT_CALL(drained, ready());
  EXPECT_CALL(callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent event) -> void {
        EXPECT_EQ(Network::ConnectionEvent::RemoteClose, event);
        c1.releaseConn();
      }));
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_CALL(conn_pool_, onConnDestroyedForTest());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test connecting_request_capacity logic.
 */
TEST_P(TcpConnPoolImplTest, RequestCapacity) {
  if (!test_new_connection_pool_) {
    return;
  }
  cluster_->resetResourceManager(5, 1024, 1024, 1, 1);
  cluster_->max_requests_per_connection_ = 100;

  ConnPoolCallbacks callbacks1;
  ConnPoolCallbacks callbacks2;
  Tcp::ConnectionPool::Cancellable* handle1;
  Tcp::ConnectionPool::Cancellable* handle2;
  {
    // Request 1 should kick off a new connection.
    conn_pool_.expectConnCreate();
    handle1 = conn_pool_.newConnection(callbacks1);
    EXPECT_NE(nullptr, handle1);
  }
  {
    // Request 2 should kick off a new connection.
    conn_pool_.expectConnCreate();
    handle2 = conn_pool_.newConnection(callbacks2);
    EXPECT_NE(nullptr, handle2);
  }

  // This should set the number of requests remaining to 1 on the active
  // connections, and the connecting_request_capacity to 2 as well.
  conn_pool_.drainConnections();

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
    handle3 = conn_pool_.newConnection(callbacks3);
    EXPECT_NE(nullptr, handle3);

    handle4 = conn_pool_.newConnection(callbacks4);
    EXPECT_NE(nullptr, handle4);
  }
  {
    // With connecting_request_capacity zero, a request for a new connection
    // will kick off connection #3.
    conn_pool_.expectConnCreate();
    handle5 = conn_pool_.newConnection(callbacks5);
    EXPECT_NE(nullptr, handle5);
  }

  // Clean up remaining connections.
  handle3->cancel(ConnectionPool::CancelPolicy::Default);
  handle4->cancel(ConnectionPool::CancelPolicy::Default);
  handle5->cancel(ConnectionPool::CancelPolicy::Default);
  conn_pool_.test_conns_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_.test_conns_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  conn_pool_.test_conns_[2].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

/**
 * Test that pending connections are closed when the connection pool is destroyed.
 */
TEST_P(TcpConnPoolImplDestructorTest, TestPendingConnectionsAreClosed) {
  connection_ = new NiceMock<Network::MockClientConnection>();
  connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
  EXPECT_CALL(*connect_timer_, enableTimer(_, _));

  callbacks_ = std::make_unique<ConnPoolCallbacks>();
  ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*callbacks_);
  EXPECT_NE(nullptr, handle);

  EXPECT_CALL(callbacks_->pool_failure_, ready());
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}

/**
 * Test that busy connections are closed when the connection pool is destroyed.
 */
TEST_P(TcpConnPoolImplDestructorTest, TestBusyConnectionsAreClosed) {
  prepareConn();

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}

/**
 * Test that ready connections are closed when the connection pool is destroyed.
 */
TEST_P(TcpConnPoolImplDestructorTest, TestReadyConnectionsAreClosed) {
  prepareConn();

  // Transition connection to ready list
  callbacks_->conn_data_.reset();

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(dispatcher_, clearDeferredDeleteList());
  conn_pool_.reset();
}
INSTANTIATE_TEST_SUITE_P(ConnectionPools, TcpConnPoolImplTest, testing::Bool());
INSTANTIATE_TEST_SUITE_P(ConnectionPools, TcpConnPoolImplDestructorTest, testing::Bool());

} // namespace Tcp
} // namespace Envoy
