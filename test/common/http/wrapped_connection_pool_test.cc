#include "envoy/http/codec.h"

#include "common/http/wrapped_connection_pool.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/http/mock_connection_mapper.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {

class WrappedConnectionPoolTest : public testing::Test {
public:
  std::unique_ptr<WrappedConnectionPool> createWrappedPool(Protocol protocol = Protocol::Http11) {
    auto mapper_mock = std::make_unique<MockConnectionMapper>();
    conn_mapper_mock_ = mapper_mock.get();

    EXPECT_CALL(*conn_mapper_mock_, addIdleCallback_);
    return std::make_unique<WrappedConnectionPool>(std::move(mapper_mock), protocol, host_,
                                                   Upstream::ResourcePriority::Default);
  }

  //! Sets the test up to return @c wrapped_pool_ from conn_mapper_mock
  void expectSimpleConnPoolReturn(size_t num_times = 1) {
    EXPECT_CALL(*conn_mapper_mock_, assignPool_(&lb_context_mock_))
        .Times(num_times)
        .WillRepeatedly(Return(&wrapped_pool_));
  }

  //! Sets the test up for the mapper to "fail" by returning nullptr
  void expectNoConnPoolReturn(size_t num_times = 1) {
    EXPECT_CALL(*conn_mapper_mock_, assignPool_(&lb_context_mock_))
        .Times(num_times)
        .WillRepeatedly(Return(nullptr));
  }

  void expectNumPending(size_t number) {
    EXPECT_EQ(cluster_->stats_.upstream_rq_pending_active_.value(), number);
  }

  Upstream::MockLoadBalancerContext lb_context_mock_;
  MockConnectionMapper* conn_mapper_mock_ = nullptr;
  MockStreamDecoder stream_decoder_mock_;
  ConnPoolCallbacks callbacks_;
  ConnectionPool::MockInstance wrapped_pool_;
  NiceMock<ConnectionPool::MockCancellable> cancellable_mock_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:9000")};
};

TEST_F(WrappedConnectionPoolTest, PoolAssignedNoCancellable) {
  auto pool = createWrappedPool();

  expectSimpleConnPoolReturn();
  EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).WillOnce(Return(nullptr));

  const auto cancellable = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  EXPECT_EQ(cancellable, nullptr);
  expectNumPending(0);
}

TEST_F(WrappedConnectionPoolTest, PoolAssignedCancellable) {
  auto pool = createWrappedPool();

  expectSimpleConnPoolReturn();
  EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).WillOnce(Return(&cancellable_mock_));

  const auto cancellable = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  EXPECT_EQ(cancellable, &cancellable_mock_);
  expectNumPending(0);
}

TEST_F(WrappedConnectionPoolTest, ProtocolHttp11) {
  auto pool = createWrappedPool(Protocol::Http11);

  EXPECT_EQ(pool->protocol(), Protocol::Http11);
}

TEST_F(WrappedConnectionPoolTest, ProtocolHttp2) {
  auto pool = createWrappedPool(Protocol::Http2);

  EXPECT_EQ(pool->protocol(), Protocol::Http2);
}

TEST_F(WrappedConnectionPoolTest, AssignPoolFailureMakesPending) {
  auto pool = createWrappedPool();

  expectNoConnPoolReturn();

  EXPECT_NE(pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_), nullptr);

  expectNumPending(1);
}

TEST_F(WrappedConnectionPoolTest, TwoAssignFailuresQueuesThemUp) {
  auto pool = createWrappedPool();

  expectNoConnPoolReturn(2);

  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  EXPECT_NE(pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_), nullptr);
  expectNumPending(2);
}

TEST_F(WrappedConnectionPoolTest, PendingCancelledCausesDrain) {

  auto pool = createWrappedPool();

  expectNoConnPoolReturn(1);
  ReadyWatcher drained;
  EXPECT_CALL(drained, ready());
  pool->addDrainedCallback([&drained]() -> void { drained.ready(); });

  auto cancellable = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  cancellable->cancel();

  expectNumPending(0);
}

// Shows that if there are still pending connections, a cancel won't lead to a drain callback.
TEST_F(WrappedConnectionPoolTest, OneCancelTwoPendingNoDrain) {

  auto pool = createWrappedPool();

  expectNoConnPoolReturn(2);
  ReadyWatcher drained;
  EXPECT_CALL(drained, ready()).Times(0);

  pool->addDrainedCallback([&drained]() -> void { drained.ready(); });
  auto cancellable1 = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  cancellable1->cancel();

  expectNumPending(1);
}

// Since we know that a single cancel doesn't cause a drain, if a second cancel causes a drain, then
// we know that the implementation waits for all pending requests to be cancelled before invoking
// the callback.
TEST_F(WrappedConnectionPoolTest, TwoCancelTwoPendingDrains) {

  auto pool = createWrappedPool();

  expectNoConnPoolReturn(2);
  ReadyWatcher drained;
  EXPECT_CALL(drained, ready()).Times(1);
  pool->addDrainedCallback([&drained]() -> void { drained.ready(); });

  auto cancellable1 = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  auto cancellable2 = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  cancellable1->cancel();
  cancellable2->cancel();

  expectNumPending(0);
}

// Shows that when constrained by pending requests, we overflow and increment the appropriate
// counters.
TEST_F(WrappedConnectionPoolTest, TestOverflowPending) {
  cluster_->resetResourceManager(1, 1, 1024, 1);

  auto pool = createWrappedPool();

  expectNoConnPoolReturn(2);
  auto cancellable1 = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  EXPECT_CALL(callbacks_.pool_failure_, ready());

  auto cancellable2 = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  EXPECT_NE(cancellable1, nullptr);
  EXPECT_EQ(cancellable2, nullptr);
  EXPECT_EQ(cluster_->stats_.upstream_rq_pending_overflow_.value(), 1);
  EXPECT_EQ(cluster_->stats_.upstream_rq_total_.value(), 1);
  EXPECT_EQ(host_->stats().rq_total_.value(), 1);
  EXPECT_EQ(callbacks_.failure_reason_, ConnectionPool::PoolFailureReason::Overflow);
  EXPECT_EQ(callbacks_.host_, nullptr);
  expectNumPending(1);
}

//! Makes sure that we register ourselves for an idle callback on construction
TEST_F(WrappedConnectionPoolTest, TestIdleCallbackRegistered) {

  auto pool = createWrappedPool();

  EXPECT_EQ(conn_mapper_mock_->idle_callbacks_.size(), 1);
}

//! If there aren't any pending requests, we don't expect any calls to allocate if a pool
//! is idle.
TEST_F(WrappedConnectionPoolTest, TestIdleCallbackNoPending) {

  auto pool = createWrappedPool();
  expectNoConnPoolReturn(0);

  conn_mapper_mock_->idle_callbacks_[0]();
}

//! Tests that if there is a single pending request, we'll call back with it when told that here are
//! idle pools.
TEST_F(WrappedConnectionPoolTest, TestIdleCallbackWithSinglePending) {

  auto pool = createWrappedPool();
  expectNoConnPoolReturn(1);

  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  expectSimpleConnPoolReturn();
  EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).WillOnce(Return(nullptr));

  conn_mapper_mock_->idle_callbacks_[0]();
  expectNumPending(0);
}

//! Tests that if there is more than a single pending request, we can handle them all when we're
//! told that there are idle pools.
TEST_F(WrappedConnectionPoolTest, TestIdleCallbackWithTwoPending) {

  auto pool = createWrappedPool();
  expectNoConnPoolReturn(2);

  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  expectSimpleConnPoolReturn(2);
  EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).Times(2).WillRepeatedly(Return(nullptr));

  conn_mapper_mock_->idle_callbacks_[0]();
  expectNumPending(0);
}

//! Tests that if we skip a request for assignment, we properly move to the next.
TEST_F(WrappedConnectionPoolTest, TestIdleCallbackOneSkippedOneAssigned) {

  auto pool = createWrappedPool();
  expectNoConnPoolReturn(2);

  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);
  {
    testing::InSequence sequence;
    expectNoConnPoolReturn();
    expectSimpleConnPoolReturn();
    EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).WillOnce(Return(nullptr));
    conn_mapper_mock_->idle_callbacks_[0]();
  }

  expectNumPending(1);
}

//! Tests that if the partitioned pool returns a pending response, and the response is cancelled,
//! then the partitioned pool is informed by calling the callback it provided.
TEST_F(WrappedConnectionPoolTest, TestPendingToPendingThenCancel) {

  auto pool = createWrappedPool();

  expectNoConnPoolReturn();

  auto cancellable = pool->newStream(stream_decoder_mock_, callbacks_, lb_context_mock_);

  expectSimpleConnPoolReturn();
  EXPECT_CALL(wrapped_pool_, newStream(_, _, _)).WillOnce(Return(&cancellable_mock_));
  EXPECT_CALL(cancellable_mock_, cancel());

  conn_mapper_mock_->idle_callbacks_[0]();

  cancellable->cancel();
  EXPECT_EQ(pool->numWaitingStreams(), 0);
}

} // namespace Http
} // namespace Envoy
