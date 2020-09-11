#include "common/conn_pool/conn_pool_base.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace ConnectionPool {

using testing::InvokeWithoutArgs;
using testing::Return;

class TestActiveClient : public ActiveClient {
public:
  TestActiveClient(ConnPoolImplBase& parent, uint64_t lifetime_stream_limit,
                   uint64_t concurrent_stream_limit)
      : ActiveClient{parent, lifetime_stream_limit, concurrent_stream_limit} {
    ON_CALL(*this, close).WillByDefault(Invoke([this]() {
      onEvent(Network::ConnectionEvent::LocalClose);
    }));
    ON_CALL(*this, id).WillByDefault(Return(1));
    ON_CALL(*this, closingWithIncompleteStream).WillByDefault(Return(false));
    ON_CALL(*this, numActiveStreams).WillByDefault(Return(1));
  }

  MOCK_METHOD(void, close, (), (override));
  MOCK_METHOD(uint64_t, id, (), (const override));
  MOCK_METHOD(bool, closingWithIncompleteStream, (), (const override));
  MOCK_METHOD(size_t, numActiveStreams, (), (const override));
};

class TestPendingStream : public PendingStream {
public:
  TestPendingStream(ConnPoolImplBase& parent, AttachContext& context)
      : PendingStream(parent), context_(context) {}
  AttachContext& context() override { return context_; }
  AttachContext& context_;
};

class TestConnPoolImplBase : public ConnPoolImplBase {
public:
  using ConnPoolImplBase::ConnPoolImplBase;
  ConnectionPool::Cancellable* newPendingStream(AttachContext& context) override {
    auto entry = std::make_unique<TestPendingStream>(*this, context);
    return addToPendingStreamsList(std::move(entry));
  }
  MOCK_METHOD(ActiveClientPtr, instantiateActiveClient, ());
  MOCK_METHOD(void, onPoolFailure,
              (const Upstream::HostDescriptionConstSharedPtr& n, absl::string_view,
               ConnectionPool::PoolFailureReason, AttachContext&));
  MOCK_METHOD(void, onPoolReady, (ActiveClient&, AttachContext&));
};

class ConnPoolImplBaseTest : public testing::Test {
public:
  void initialize(std::chrono::milliseconds pool_idle_timeout = std::chrono::milliseconds::max()) {
    pool_ =
        std::make_unique<TestConnPoolImplBase>(host_, Upstream::ResourcePriority::Default,
                                               dispatcher_, nullptr, nullptr, pool_idle_timeout);
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
    ON_CALL(*pool_, instantiateActiveClient).WillByDefault(Invoke([&]() -> ActiveClientPtr {
      auto ret =
          std::make_unique<NiceMock<TestActiveClient>>(*pool_, stream_limit_, concurrent_streams_);
      clients_.push_back(ret.get());
      ret->real_host_description_ = descr_;
      return ret;
    }));
  }

  ConnPoolImplBaseTest() = default;

  uint32_t stream_limit_ = 100;
  uint32_t concurrent_streams_ = 1;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> descr_{
      new NiceMock<Upstream::MockHostDescription>()};
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80")};
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<TestConnPoolImplBase> pool_;
  AttachContext context_;
  std::vector<NiceMock<TestActiveClient>*> clients_;
};

TEST_F(ConnPoolImplBaseTest, BasicPrefetch) {
  initialize();
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  EXPECT_CALL(*pool_, instantiateActiveClient).Times(2);
  auto cancelable = pool_->newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_->destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, PrefetchOnDisconnect) {
  initialize();
  testing::InSequence s;

  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  // On new stream, create 2 connections.
  EXPECT_CALL(*pool_, instantiateActiveClient).Times(2);
  pool_->newStream(context_);

  // If a connection fails, existing connections are purged. If a retry causes
  // a new stream, make sure we create the correct number of connections.
  EXPECT_CALL(*pool_, onPoolFailure).WillOnce(InvokeWithoutArgs([&]() -> void {
    pool_->newStream(context_);
  }));
  EXPECT_CALL(*pool_, instantiateActiveClient).Times(1);
  clients_[0]->close();

  EXPECT_CALL(*pool_, onPoolFailure);
  pool_->destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPrefetchIfUnhealthy) {
  initialize();
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  host_->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(host_->health(), Upstream::Host::Health::Unhealthy);

  // On new stream, create 1 connection.
  EXPECT_CALL(*pool_, instantiateActiveClient).Times(1);
  auto cancelable = pool_->newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_->destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, NoPrefetchIfDegraded) {
  initialize();
  // Create more than one connection per new stream.
  ON_CALL(*cluster_, prefetchRatio).WillByDefault(Return(1.5));

  EXPECT_EQ(host_->health(), Upstream::Host::Health::Healthy);
  host_->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_EQ(host_->health(), Upstream::Host::Health::Degraded);

  // On new stream, create 1 connection.
  EXPECT_CALL(*pool_, instantiateActiveClient).Times(1);
  auto cancelable = pool_->newStream(context_);

  cancelable->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  pool_->destructAllConnections();
}

TEST_F(ConnPoolImplBaseTest, TimeoutTest) {
  static constexpr std::chrono::milliseconds IDLE_TIMEOUT{5000};

  // Default behavior for other timers associated with the pool
  EXPECT_CALL(dispatcher_, createTimer_(_));

  // Inject this timer into the pool so we can add some expectations
  auto* timer_ptr = new NiceMock<Event::MockTimer>(&dispatcher_);
  initialize(IDLE_TIMEOUT);

  testing::MockFunction<void()> idle_pool_callback;
  pool_->addIdlePoolTimeoutCallbackImpl(idle_pool_callback.AsStdFunction());

  // Create a new stream using the pool
  EXPECT_CALL(*pool_, instantiateActiveClient);
  pool_->newStream(context_);
  ASSERT_EQ(1, clients_.size());

  // Emulate the new upstream connection establishment
  EXPECT_CALL(*pool_, onPoolReady);
  clients_.back()->onEvent(Network::ConnectionEvent::Connected);

  // Close the newly-created stream and expect the timer to be set
  EXPECT_CALL(*timer_ptr, enableTimer(IDLE_TIMEOUT, _));
  EXPECT_CALL(*clients_.back(), numActiveStreams).WillRepeatedly(Return(0));
  pool_->onStreamClosed(*clients_.back(), false);
  clients_.back()->close();

  // Emulate the idle timeout firing and expect our callback to be triggered
  EXPECT_CALL(idle_pool_callback, Call).Times(1);
  timer_ptr->invokeCallback();
}

} // namespace ConnectionPool
} // namespace Envoy
