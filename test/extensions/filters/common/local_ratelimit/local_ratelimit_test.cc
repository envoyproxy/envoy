#include "source/common/singleton/manager_impl.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

class WrapperedProvider {
public:
  WrapperedProvider(ShareProviderSharedPtr provider) : provider_(provider) {}

  uint32_t tokensPerFill(uint32_t origin_tokens_per_fill) const {
    return std::ceil(origin_tokens_per_fill * provider_->getTokensShareFactor());
  }

  ShareProviderSharedPtr provider_;
};

TEST(ShareProviderManagerTest, ShareProviderManagerTest) {
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Event::MockDispatcher> dispatcher;
  Singleton::ManagerImpl manager;

  NiceMock<Upstream::MockPrioritySet> priority_set;
  cm.local_cluster_name_ = "local_cluster";
  cm.initializeClusters({"local_cluster"}, {});

  const auto* mock_local_cluster = cm.active_clusters_.at("local_cluster").get();

  EXPECT_CALL(*mock_local_cluster, prioritySet()).WillOnce(ReturnRef(priority_set));
  EXPECT_CALL(priority_set, addMemberUpdateCb(_));

  // Set the membership total to 2.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(2);

  ShareProviderManagerSharedPtr share_provider_manager =
      ShareProviderManager::singleton(dispatcher, cm, manager);
  EXPECT_NE(share_provider_manager, nullptr);

  auto provider = std::make_shared<WrapperedProvider>(
      share_provider_manager->getShareProvider(ProtoLocalClusterRateLimit()));

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(1, provider->tokensPerFill(2));
  EXPECT_EQ(2, provider->tokensPerFill(4));
  EXPECT_EQ(4, provider->tokensPerFill(8));

  // Set the membership total to 4.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(4);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(1, provider->tokensPerFill(4));
  EXPECT_EQ(2, provider->tokensPerFill(8));
  EXPECT_EQ(4, provider->tokensPerFill(16));

  // Set the membership total to 0.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(0);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));

  // Set the membership total to 1.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(1);
  priority_set.runUpdateCallbacks(0, {}, {});

  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));

  // Destroy the share provider manager.
  // This is used to ensure the share provider is still safe to use even
  // the share provider manager is destroyed. But note this should never
  // happen in real production because the share provider manager should
  // have longer life cycle than the limiter.
  share_provider_manager.reset();

  // Set the membership total to 4 again.
  mock_local_cluster->info_->endpoint_stats_.membership_total_.set(4);
  priority_set.runUpdateCallbacks(0, {}, {});

  // The provider should still work but the value should not change.
  EXPECT_EQ(1, provider->tokensPerFill(1)); // At least 1 token per fill.
  EXPECT_EQ(2, provider->tokensPerFill(2));
  EXPECT_EQ(4, provider->tokensPerFill(4));
  EXPECT_EQ(8, provider->tokensPerFill(8));
}

class MockShareProvider : public ShareProvider {
public:
  MockShareProvider() = default;
  MOCK_METHOD(double, getTokensShareFactor, (), (const));
};

class LocalRateLimiterImplTest : public testing::Test {
public:
  void initializeTimer() {
    fill_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*fill_timer_, enableTimer(_, nullptr));
    EXPECT_CALL(*fill_timer_, disableTimer());
  }

  void initialize(const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
                  const uint32_t tokens_per_fill, ShareProviderSharedPtr share_provider = nullptr) {
    TestScopedRuntime runtime;
    runtime.mergeValues(
        {{"envoy.reloadable_features.no_timer_based_rate_limit_token_bucket", "false"}});

    initializeTimer();

    rate_limiter_ =
        std::make_shared<LocalRateLimiterImpl>(fill_interval, max_tokens, tokens_per_fill,
                                               dispatcher_, descriptors_, true, share_provider);
  }

  void initializeWithAtomicTokenBucket(const std::chrono::milliseconds fill_interval,
                                       const uint32_t max_tokens, const uint32_t tokens_per_fill,
                                       ShareProviderSharedPtr share_provider = nullptr) {
    rate_limiter_ =
        std::make_shared<LocalRateLimiterImpl>(fill_interval, max_tokens, tokens_per_fill,
                                               dispatcher_, descriptors_, true, share_provider);
  }

  Thread::ThreadSynchronizer& synchronizer() { return rate_limiter_->synchronizer_; }
  Envoy::Protobuf::RepeatedPtrField<
      envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>
      descriptors_;

  std::vector<Envoy::RateLimit::LocalDescriptor> route_descriptors_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* fill_timer_{};
  std::shared_ptr<LocalRateLimiterImpl> rate_limiter_;
};

// Make sure we fail with a fill rate this is too fast.
TEST_F(LocalRateLimiterImplTest, TooFastFillRate) {
  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(49), 100, 1, dispatcher_, descriptors_),
      EnvoyException, "local rate limit token bucket fill timer must be >= 50ms");
}

// Verify various token bucket CAS edge cases.
TEST_F(LocalRateLimiterImplTest, CasEdgeCases) {
  // This tests the case in which an allowed check races with the fill timer.
  {
    initialize(std::chrono::milliseconds(50), 1, 1);

    synchronizer().enable();

    // Start a thread and start the fill callback. This will wait pre-CAS.
    synchronizer().waitOn("on_fill_timer_pre_cas");
    std::thread t1([&] {
      EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
      fill_timer_->invokeCallback();
    });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("on_fill_timer_pre_cas");

    // This should succeed.
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
    EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  }

  // This tests the case in which two allowed checks race.
  {
    initialize(std::chrono::milliseconds(200), 1, 1);

    synchronizer().enable();

    // Start a thread and see if we are under limit. This will wait pre-CAS.
    synchronizer().waitOn("allowed_pre_cas");
    std::thread t1(
        [&] { EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("allowed_pre_cas");

    // Consume a token on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
    synchronizer().signal("allowed_pre_cas");
    t1.join();
  }
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterImplTest, TokenBucket) {
  initialize(std::chrono::milliseconds(200), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimiterImplTest, TokenBucketMultipleTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 1 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1 and
// share provider is used.
TEST_F(LocalRateLimiterImplTest, TokenBucketMultipleTokensPerFillWithShareProvider) {
  auto share_provider = std::make_shared<MockShareProvider>();
  EXPECT_CALL(*share_provider, getTokensShareFactor())
      .WillRepeatedly(testing::Invoke([]() -> double { return 0.5; }));

  // Final tokens per fill is 2/2 = 1.
  initialize(std::chrono::milliseconds(200), 2, 2, share_provider);

  // The limiter will be initialized with max tokens and it will not be shared.
  // So, the initial tokens is 2.
  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // The tokens per fill will be handled by the share provider and it will be 1.
  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimiterImplTest, TokenBucketMaxTokensGreaterThanTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 1);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterImplTest, TokenBucketStatus) {
  initialize(std::chrono::milliseconds(3000), 2, 2);

  // 2 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(3000), nullptr));
  auto rate_limit_result = rate_limiter_->requestAllowed(route_descriptors_);
  EXPECT_TRUE(rate_limit_result.allowed);

  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3.0);

  // 1 -> 0 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 2.0);

  // 0 -> 0 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 1.0);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  fill_timer_->invokeCallback();

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3.0);
}

class LocalRateLimiterDescriptorImplTest : public LocalRateLimiterImplTest {
public:
  void initializeWithDescriptor(const std::chrono::milliseconds fill_interval,
                                const uint32_t max_tokens, const uint32_t tokens_per_fill) {

    TestScopedRuntime runtime;
    runtime.mergeValues(
        {{"envoy.reloadable_features.no_timer_based_rate_limit_token_bucket", "false"}});

    initializeTimer();

    rate_limiter_ = std::make_shared<LocalRateLimiterImpl>(
        fill_interval, max_tokens, tokens_per_fill, dispatcher_, descriptors_);
  }

  void initializeWithAtomicTokenBucketDescriptor(const std::chrono::milliseconds fill_interval,
                                                 const uint32_t max_tokens,
                                                 const uint32_t tokens_per_fill) {
    rate_limiter_ = std::make_shared<LocalRateLimiterImpl>(
        fill_interval, max_tokens, tokens_per_fill, dispatcher_, descriptors_);
  }

  static constexpr absl::string_view single_descriptor_config_yaml = R"(
  entries:
  - key: foo2
    value: bar2
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
  )";

  const std::string multiple_descriptor_config_yaml = R"(
  entries:
  - key: hello
    value: world
  - key: foo
    value: bar
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1s
  )";

  // Default token bucket
  std::vector<RateLimit::LocalDescriptor> descriptor_{{{{"foo2", "bar2"}}}};
  std::vector<RateLimit::LocalDescriptor> descriptor2_{{{{"hello", "world"}, {"foo", "bar"}}}};
};

// Verify descriptor rate limit time interval is multiple of token bucket fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorRateLimitDivisibleByTokenFillInterval) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 10, 10, "60s"),
                            *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(59000), 2, 1, dispatcher_, descriptors_),
      EnvoyException, "local rate descriptor limit is not a multiple of token bucket fill timer");
}

TEST_F(LocalRateLimiterDescriptorImplTest, DuplicateDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(50), 1, 1, dispatcher_, descriptors_),
      EnvoyException, "duplicate descriptor in the local rate descriptor: foo2=bar2");
}

// Verify no exception for per route config without descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorRateLimitNoExceptionWithoutDescriptor) {
  VERBOSE_EXPECT_NO_THROW(
      LocalRateLimiterImpl(std::chrono::milliseconds(59000), 2, 1, dispatcher_, descriptors_));
}

// Verify various token bucket CAS edge cases for descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, CasEdgeCasesDescriptor) {
  // This tests the case in which an allowed check races with the fill timer.
  {
    TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                              *descriptors_.Add());
    initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();

    synchronizer().enable();

    // Start a thread and start the fill callback. This will wait pre-CAS.
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    synchronizer().waitOn("on_fill_timer_pre_cas");
    std::thread t1([&] {
      EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
      fill_timer_->invokeCallback();
    });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("on_fill_timer_pre_cas");

    // This should succeed.
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
    EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  }

  // This tests the case in which two allowed checks race.
  {
    initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

    synchronizer().enable();

    // Start a thread and see if we are under limit. This will wait pre-CAS.
    synchronizer().waitOn("allowed_pre_cas");
    std::thread t1([&] { EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("allowed_pre_cas");

    // Consume a token on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
    synchronizer().signal("allowed_pre_cas");
    t1.join();
  }
}

TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptor2) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(100), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 1 -> 1 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with request per unit > 1.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketMultipleTokensPerFillDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 2 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 1 -> 2 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDifferentDescriptorDifferentRateLimits) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "2s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(1000), 3, 1);

  // 1 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens for descriptor2_ and 0 only for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors sorted.
TEST_F(LocalRateLimiterDescriptorImplTest,
       TokenBucketDifferentDescriptorDifferentRateLimitsSorted) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 3, 3);
  std::vector<RateLimit::LocalDescriptor> descriptors{{{{"hello", "world"}, {"foo", "bar"}}},
                                                      {{{"foo2", "bar2"}}}};

  // Descriptors are sorted as descriptor2 < descriptor < global
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Request limited by descriptor2 will not consume tokens from descriptor.
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptorStatus) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(1000), 2, 2);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(descriptor_);

  EXPECT_TRUE(rate_limit_result.allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3.0);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 2.0);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 1.0);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 0 -> 2 tokens
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3.0);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval with
// multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDifferentDescriptorStatus) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(1000), 20, 20);

  // 2 -> 1 tokens for descriptor_
  auto rate_limit_result = rate_limiter_->requestAllowed(descriptor_);

  EXPECT_TRUE(rate_limit_result.allowed);
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3);

  // 1 -> 0 tokens for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3);

  // 1 -> 0 tokens for descriptor2_
  auto rate_limit_result2 = rate_limiter_->requestAllowed(descriptor2_);
  EXPECT_TRUE(rate_limit_result2.allowed);

  EXPECT_EQ(rate_limit_result2.token_bucket_context->maxTokens(), 1);
  EXPECT_EQ(rate_limit_result2.token_bucket_context->remainingTokens(), 0);
  EXPECT_EQ(rate_limit_result2.token_bucket_context->remainingFillInterval().value(), 1);

  // 0 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  EXPECT_EQ(rate_limit_result2.token_bucket_context->maxTokens(), 1);
  EXPECT_EQ(rate_limit_result2.token_bucket_context->remainingTokens(), 1);
  EXPECT_EQ(rate_limit_result2.token_bucket_context->remainingFillInterval().value(), 1);

  // 0 -> 2 tokens for descriptor_
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
    fill_timer_->invokeCallback();
  }

  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingFillInterval().value(), 3.0);
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucket) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMultipleTokensPerFill) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 1 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens and tokens per fill > 1 and
// share provider is used.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMultipleTokensPerFillWithShareProvider) {
  auto share_provider = std::make_shared<MockShareProvider>();
  EXPECT_CALL(*share_provider, getTokensShareFactor())
      .WillRepeatedly(testing::Invoke([]() -> double { return 0.5; }));

  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 2, share_provider);

  // Every request will consume 1 / factor = 2 tokens.

  // The limiter will be initialized with max tokens and will be consumed at once.
  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketMaxTokensGreaterThanTokensPerFill) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(200), 2, 1);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterImplTest, AtomicTokenBucketStatus) {
  initializeWithAtomicTokenBucket(std::chrono::milliseconds(3000), 2, 2);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(route_descriptors_);
  EXPECT_TRUE(rate_limit_result.allowed);

  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1500));

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_).allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(3000));

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
}

TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptorBase) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);

  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 1 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with request per unit > 1.
TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketMultipleTokensPerFillDescriptor) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "0.1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(100));

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 1 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(50));

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest,
       AtomicTokenBucketDifferentDescriptorDifferentRateLimits) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 1, 1, "2s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 3, 3);

  // 1 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // 1 -> 0 tokens for descriptor2_ and 0 only for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_).allowed);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket functionality with multiple descriptors sorted.
TEST_F(LocalRateLimiterDescriptorImplTest,
       AtomicTokenBucketDifferentDescriptorDifferentRateLimitsSorted) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(50), 3, 3);

  std::vector<RateLimit::LocalDescriptor> descriptors{{{{"hello", "world"}, {"foo", "bar"}}},
                                                      {{{"foo2", "bar2"}}}};

  // Descriptors are sorted as descriptor2 < descriptor < global
  // Descriptor2 from 1 -> 0 tokens
  // Descriptor from 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Request limited by descriptor2 and won't consume tokens from descriptor.
  // Descriptor2 from 0 -> 0 tokens
  // Descriptor from 1 -> 1 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors).allowed);
  // Descriptor from 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Descriptor from 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_).allowed);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, AtomicTokenBucketDescriptorStatus) {
  TestUtility::loadFromYaml(fmt::format(single_descriptor_config_yaml, 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithAtomicTokenBucketDescriptor(std::chrono::milliseconds(1000), 2, 2);

  // 2 -> 1 tokens
  auto rate_limit_result = rate_limiter_->requestAllowed(descriptor_);

  EXPECT_TRUE(rate_limit_result.allowed);

  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 1);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(500));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  // 0 -> 1 tokens. 1500ms passed and 1 token will be added.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(1000));

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_).allowed);
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 0);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(3000));

  // 0 -> 2 tokens
  // Note that the route descriptors are not changed so we can reuse the same token bucket context.
  EXPECT_EQ(rate_limit_result.token_bucket_context->maxTokens(), 2);
  EXPECT_EQ(rate_limit_result.token_bucket_context->remainingTokens(), 2);
}

} // Namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
