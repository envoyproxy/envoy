#include <chrono>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/stats/stats.h"

#include "common/http/header_map_impl.h"
#include "common/router/retry_state_impl.h"
#include "common/upstream/resource_manager_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Router {
namespace {

class RouterRetryStateImplTest : public testing::Test {
public:
  enum TestResourceType { Connection, Request, PendingRequest, Retry };

  RouterRetryStateImplTest() : callback_([this]() -> void { callback_ready_.ready(); }) {
    ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
        .WillByDefault(Return(true));
  }

  void setup() {
    Http::TestRequestHeaderMapImpl headers;
    setup(headers);
  }

  void setup(Http::RequestHeaderMap& request_headers) {
    state_ = RetryStateImpl::create(policy_, request_headers, cluster_, runtime_, random_,
                                    dispatcher_, Upstream::ResourcePriority::Default);
  }

  void expectRecordAndWouldRetryFromHeaders() {
    EXPECT_CALL(policy_, recordResponseHeaders(_)).Times(1);
    EXPECT_CALL(policy_, wouldRetryFromHeaders(_)).WillOnce(Return(true));
  }

  void expectTimerCreateAndEnable() {
    retry_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  }

  void incrOutstandingResource(TestResourceType resource, uint32_t num) {
    for (uint32_t i = 0; i < num; ++i) {
      switch (resource) {
      case TestResourceType::Retry:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).retries().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).retries().dec();
        });
        break;
      case TestResourceType::Connection:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).connections().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).connections().dec();
        });
        break;
      case TestResourceType::Request:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).requests().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).requests().dec();
        });
        break;
      case TestResourceType::PendingRequest:
        cluster_.resourceManager(Upstream::ResourcePriority::Default).pendingRequests().inc();
        resource_manager_cleanup_tasks_.emplace_back([this]() {
          cluster_.resourceManager(Upstream::ResourcePriority::Default).pendingRequests().dec();
        });
        break;
      }
    }
  }

  void cleanupOutstandingResources() {
    for (auto& task : resource_manager_cleanup_tasks_) {
      task();
    }
    resource_manager_cleanup_tasks_.clear();
  }

  void TearDown() override { cleanupOutstandingResources(); }

  NiceMock<TestRetryPolicy> policy_;
  NiceMock<Upstream::MockClusterInfo> cluster_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_{};
  RetryStatePtr state_;
  ReadyWatcher callback_ready_;
  RetryState::DoRetryCallback callback_;
  std::vector<std::function<void()>> resource_manager_cleanup_tasks_;
  Http::TestResponseHeaderMapImpl response_headers_;

  const Http::StreamResetReason remote_reset_{Http::StreamResetReason::RemoteReset};
  const Http::StreamResetReason remote_refused_stream_reset_{
      Http::StreamResetReason::RemoteRefusedStreamReset};
  const Http::StreamResetReason overflow_reset_{Http::StreamResetReason::Overflow};
  const Http::StreamResetReason connect_failure_{Http::StreamResetReason::ConnectionFailure};
};

TEST_F(RouterRetryStateImplTest, NoneRetryHeader) {
  EXPECT_CALL(policy_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(policy_, recordRequestHeader(_)).Times(1);
  setup();
  EXPECT_NE(nullptr, state_);
  EXPECT_TRUE(state_->enabled());
}

TEST_F(RouterRetryStateImplTest, NoneRetryPolicy) {
  Http::TestRequestHeaderMapImpl request_headers{{"x-envoy-retry-on", "refused-stream"}};
  EXPECT_CALL(policy_, recordRequestHeader(_)).Times(1);
  setup(request_headers);
  EXPECT_NE(nullptr, state_);
  EXPECT_FALSE(state_->enabled());
}

TEST_F(RouterRetryStateImplTest, PolicyWouldRetryFromReset) {
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(remote_refused_stream_reset_)).Times(2);
  EXPECT_CALL(policy_, wouldRetryFromReset(remote_refused_stream_reset_))
      .Times(2)
      .WillRepeatedly(Return(true));
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(remote_refused_stream_reset_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryReset(remote_refused_stream_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyWouldNotRetryFromReset) {
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(overflow_reset_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromReset(overflow_reset_)).WillOnce(Return(false));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(overflow_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyWouldRetryFromHeaders) {
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordResponseHeaders(_)).Times(2);
  EXPECT_CALL(policy_, wouldRetryFromHeaders(_)).Times(2).WillRepeatedly(Return(true));
  expectTimerCreateAndEnable();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, PolicyWouldNotRetryFromHeaders) {
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordResponseHeaders(_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromHeaders(_)).WillOnce(Return(false));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, RuntimeGuard) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillOnce(Return(false));

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(remote_reset_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromReset(remote_reset_)).WillOnce(Return(true));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryReset(remote_reset_, callback_));
}

TEST_F(RouterRetryStateImplTest, NoAvailableRetries) {
  cluster_.resetResourceManager(0, 0, 0, 0, 0);

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(connect_failure_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromReset(connect_failure_)).WillOnce(Return(true));
  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_overflow_.value());
}

TEST_F(RouterRetryStateImplTest, Backoff) {
  policy_.remaining_retries_ = policy_.num_retries_ = 3;
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(connect_failure_)).Times(3);
  EXPECT_CALL(policy_, wouldRetryFromReset(connect_failure_)).Times(3).WillRepeatedly(Return(true));

  EXPECT_CALL(random_, random()).WillOnce(Return(49));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(24), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(74), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(349));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(174), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(policy_, recordResponseHeaders(_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromHeaders(_)).WillOnce(Return(false));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(response_headers_, callback_));

  EXPECT_EQ(3UL, cluster_.stats().upstream_rq_retry_.value());
  EXPECT_EQ(1UL, cluster_.stats().upstream_rq_retry_success_.value());
  EXPECT_EQ(0UL, cluster_.circuit_breakers_stats_.rq_retry_open_.value());
}

// Test customized retry back-off intervals.
TEST_F(RouterRetryStateImplTest, CustomBackOffInterval) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;
  policy_.base_interval_ = std::chrono::milliseconds(100);
  policy_.max_interval_ = std::chrono::milliseconds(1200);
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(connect_failure_)).Times(4);
  EXPECT_CALL(policy_, wouldRetryFromReset(connect_failure_)).Times(4).WillRepeatedly(Return(true));

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(49), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(350));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(751));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(51), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(1499));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1200), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
}

// Test the default maximum retry back-off interval.
TEST_F(RouterRetryStateImplTest, CustomBackOffIntervalDefaultMax) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;
  policy_.base_interval_ = std::chrono::milliseconds(100);
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(policy_, recordReset(connect_failure_)).Times(4);
  EXPECT_CALL(policy_, wouldRetryFromReset(connect_failure_)).Times(4).WillRepeatedly(Return(true));

  EXPECT_CALL(random_, random()).WillOnce(Return(149));
  retry_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(49), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(350));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(751));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(51), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();

  EXPECT_CALL(random_, random()).WillOnce(Return(1499));
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryReset(connect_failure_, callback_));
  EXPECT_CALL(callback_ready_, ready());
  retry_timer_->invokeCallback();
}

// Check that if there are 0 remaining retries available but we get
// non-retriable headers, we return No rather than NoRetryLimitExceeded.
TEST_F(RouterRetryStateImplTest, NoPreferredOverLimitExceeded) {
  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  Http::TestResponseHeaderMapImpl bad_response_headers{{":status", "503"}};
  expectTimerCreateAndEnable();
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(bad_response_headers, callback_));

  Http::TestResponseHeaderMapImpl good_response_headers{{":status", "200"}};
  EXPECT_CALL(policy_, recordResponseHeaders(_)).Times(1);
  EXPECT_CALL(policy_, wouldRetryFromHeaders(_)).WillOnce(Return(false));
  EXPECT_EQ(RetryStatus::No, state_->shouldRetryHeaders(good_response_headers, callback_));
}

TEST_F(RouterRetryStateImplTest, HostSelectionAttempts) {
  policy_.host_selection_max_attempts_ = 2;

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_EQ(2, state_->hostSelectionMaxAttempts());
}

TEST_F(RouterRetryStateImplTest, BudgetAvailableRetries) {
  // Expect no available retries from resource manager and override the max_retries CB via retry
  // budget. As configured, there are no allowed retries via max_retries CB.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */, 0 /* conn_pool */,
      20.0 /* budget_percent */, 3 /* min_retry_concurrency */);

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetNoAvailableRetries) {
  // Expect no available retries from resource manager. Override the max_retries CB via a retry
  // budget that won't let any retries. As configured, there are 5 allowed retries via max_retries
  // CB.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 20 /* rq */, 5 /* rq_retry */, 0 /* conn_pool */,
      0 /* budget_percent */, 0 /* min_retry_concurrency */);

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetVerifyMinimumConcurrency) {
  policy_.remaining_retries_ = policy_.num_retries_ = 42;

  // Expect no available retries from resource manager.
  cluster_.resetResourceManagerWithRetryBudget(
      0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */, 0 /* conn_pool */,
      20.0 /* budget_percent */, 3 /* min_retry_concurrency */);

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  // Load up 2 outstanding retries and verify the 3rd one is allowed when there are no outstanding
  // requests. This verifies the minimum allowed outstanding retries before the budget is scaled
  // with the request concurrency.
  incrOutstandingResource(TestResourceType::Retry, 2);

  expectTimerCreateAndEnable();
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));

  // 3 outstanding retries.
  incrOutstandingResource(TestResourceType::Retry, 1);
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers_, callback_));

  incrOutstandingResource(TestResourceType::Request, 20);

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));

  // 4 outstanding retries.
  incrOutstandingResource(TestResourceType::Retry, 1);
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::NoOverflow, state_->shouldRetryHeaders(response_headers_, callback_));

  // Override via runtime and expect successful retry.
  std::string value("100");
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.budget_percent"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, getDouble("fake_clusterretry_budget.budget_percent", _))
      .WillRepeatedly(Return(100.0));

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, BudgetRuntimeSetOnly) {
  // Expect no available retries from resource manager, so no retries allowed according to
  // max_retries CB. Don't configure retry budgets. We'll rely on runtime config only.
  cluster_.resetResourceManager(0 /* cx */, 0 /* rq_pending */, 0 /* rq */, 0 /* rq_retry */,
                                0 /* conn_pool */);

  std::string value("20");
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.min_retry_concurrency"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, get("fake_clusterretry_budget.budget_percent"))
      .WillRepeatedly(Return(value));
  EXPECT_CALL(cluster_.runtime_.snapshot_, getDouble("fake_clusterretry_budget.budget_percent", _))
      .WillRepeatedly(Return(20.0));

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  incrOutstandingResource(TestResourceType::Retry, 2);

  expectTimerCreateAndEnable();
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, ChangeRemaingRetries) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());

  expectTimerCreateAndEnable();
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));

  EXPECT_EQ(policy_.remainingRetries(), 8);
  policy_.remainingRetries() = 1;

  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::Yes, state_->shouldRetryHeaders(response_headers_, callback_));

  expectRecordAndWouldRetryFromHeaders();
  EXPECT_EQ(RetryStatus::NoRetryLimitExceeded,
            state_->shouldRetryHeaders(response_headers_, callback_));
}

TEST_F(RouterRetryStateImplTest, OnHostAttempted) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;

  auto retry_host_predicate1{std::make_shared<Upstream::MockRetryHostPredicate>()},
      retry_host_predicate2{std::make_shared<Upstream::MockRetryHostPredicate>()};
  std::vector<Upstream::RetryHostPredicateSharedPtr> retry_host_predicates{retry_host_predicate1,
                                                                           retry_host_predicate2};

  const Upstream::HealthyLoad healthy_priority_load({0u, 100u});
  const Upstream::DegradedLoad degraded_priority_load({0u, 100u});
  auto retry_priority{
      std::make_shared<Upstream::MockRetryPriority>(healthy_priority_load, degraded_priority_load)};

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(policy_, retryHostPredicates()).WillOnce(Return(retry_host_predicates));
  EXPECT_CALL(policy_, retryPriority()).WillOnce(Return(retry_priority));
  setup();
  EXPECT_TRUE(state_->enabled());

  EXPECT_CALL(*retry_host_predicate1, onHostAttempted(_)).Times(1);
  EXPECT_CALL(*retry_host_predicate2, onHostAttempted(_)).Times(1);
  EXPECT_CALL(*retry_priority, onHostAttempted(_)).Times(1);
  state_->onHostAttempted(nullptr);
}

TEST_F(RouterRetryStateImplTest, ShouldSelectAnotherHost) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;

  auto retry_host_predicate1{std::make_shared<Upstream::MockRetryHostPredicate>()},
      retry_host_predicate2{std::make_shared<Upstream::MockRetryHostPredicate>()};
  std::vector<Upstream::RetryHostPredicateSharedPtr> retry_host_predicates{retry_host_predicate1,
                                                                           retry_host_predicate2};

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(policy_, retryHostPredicates()).WillOnce(Return(retry_host_predicates));
  setup();
  EXPECT_TRUE(state_->enabled());

  NiceMock<Upstream::MockHost> host;

  EXPECT_CALL(*retry_host_predicate1, shouldSelectAnotherHost(_)).WillOnce(Return(false));
  EXPECT_CALL(*retry_host_predicate2, shouldSelectAnotherHost(_)).WillOnce(Return(false));
  EXPECT_FALSE(state_->shouldSelectAnotherHost(host));

  EXPECT_CALL(*retry_host_predicate1, shouldSelectAnotherHost(_)).WillOnce(Return(false));
  EXPECT_CALL(*retry_host_predicate2, shouldSelectAnotherHost(_)).WillOnce(Return(true));
  EXPECT_TRUE(state_->shouldSelectAnotherHost(host));

  EXPECT_CALL(*retry_host_predicate1, shouldSelectAnotherHost(_)).WillOnce(Return(true));
  EXPECT_TRUE(state_->shouldSelectAnotherHost(host));

  EXPECT_CALL(*retry_host_predicate1, shouldSelectAnotherHost(_)).WillOnce(Return(true));
  EXPECT_TRUE(state_->shouldSelectAnotherHost(host));
}

TEST_F(RouterRetryStateImplTest, RetryPriorityNonePriorityLoadForRetry) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;

  Upstream::MockPrioritySet priority_set;
  Upstream::HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({100, 0, 0}),
                                                 Upstream::DegradedLoad({0, 0, 0})};

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  setup();
  EXPECT_TRUE(state_->enabled());
  auto load = state_->priorityLoadForRetry(priority_set, priority_load);
  EXPECT_EQ(priority_load.healthy_priority_load_, load.healthy_priority_load_);
  EXPECT_EQ(priority_load.degraded_priority_load_, load.degraded_priority_load_);
}

TEST_F(RouterRetryStateImplTest, PriorityLoadForRetry) {
  policy_.remaining_retries_ = policy_.num_retries_ = 10;

  Upstream::MockPrioritySet priority_set;
  Upstream::HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({100, 0, 0}),
                                                 Upstream::DegradedLoad({0, 0, 0})};

  const Upstream::HealthyLoad healthy_priority_load({0u, 100u});
  const Upstream::DegradedLoad degraded_priority_load({0u, 100u});
  auto retry_priority{
      std::make_shared<Upstream::MockRetryPriority>(healthy_priority_load, degraded_priority_load)};

  EXPECT_CALL(policy_, enabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(policy_, retryPriority()).WillOnce(Return(retry_priority));
  setup();
  EXPECT_TRUE(state_->enabled());

  auto load = state_->priorityLoadForRetry(priority_set, priority_load);
  EXPECT_EQ(load.degraded_priority_load_, degraded_priority_load);
  EXPECT_EQ(load.healthy_priority_load_, healthy_priority_load);
}

TEST_F(RouterRetryStateImplTest, ParseRetryOn) {
  // RETRY_ON_5XX             0x1
  // RETRY_ON_GATEWAY_ERROR   0x2
  // RETRY_ON_CONNECT_FAILURE 0x4
  std::string config = "5xx,gateway-error,connect-failure";
  auto result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 7);
  EXPECT_TRUE(result.second);

  config = "xxx,gateway-error,connect-failure";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 6);
  EXPECT_FALSE(result.second);

  config = " 5xx,gateway-error ,  connect-failure   ";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 7);
  EXPECT_TRUE(result.second);

  config = " 5 xx,gateway-error ,  connect-failure   ";
  result = RetryStateImpl::parseRetryOn(config);
  EXPECT_EQ(result.first, 6);
  EXPECT_FALSE(result.second);
}

TEST_F(RouterRetryStateImplTest, ParseRetryGrpcOn) {
  // RETRY_ON_GRPC_CANCELLED             0x20
  // RETRY_ON_GRPC_DEADLINE_EXCEEDED     0x40
  // RETRY_ON_GRPC_RESOURCE_EXHAUSTED    0x80
  std::string config = "cancelled,deadline-exceeded,resource-exhausted";
  auto result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 224);
  EXPECT_TRUE(result.second);

  config = "cancelled,deadline-exceeded,resource-exhaust";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 96);
  EXPECT_FALSE(result.second);

  config = "   cancelled,deadline-exceeded   ,   resource-exhausted   ";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 224);
  EXPECT_TRUE(result.second);

  config = "   cancelled,deadline-exceeded   ,   resource- exhausted   ";
  result = RetryStateImpl::parseRetryGrpcOn(config);
  EXPECT_EQ(result.first, 96);
  EXPECT_FALSE(result.second);
}

} // namespace
} // namespace Router
} // namespace Envoy
