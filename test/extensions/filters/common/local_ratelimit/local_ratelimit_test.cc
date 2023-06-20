#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "test/mocks/event/mocks.h"
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

class LocalRateLimiterImplTest : public testing::Test {
public:
  void initializeTimer() {
    fill_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*fill_timer_, enableTimer(_, nullptr));
    EXPECT_CALL(*fill_timer_, disableTimer());
  }

  void initialize(const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
                  const uint32_t tokens_per_fill) {

    initializeTimer();

    rate_limiter_ = std::make_shared<LocalRateLimiterImpl>(
        fill_interval, max_tokens, tokens_per_fill, dispatcher_, descriptors_);
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
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
    EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
  }

  // This tests the case in which two allowed checks race.
  {
    initialize(std::chrono::milliseconds(200), 1, 1);

    synchronizer().enable();

    // Start a thread and see if we are under limit. This will wait pre-CAS.
    synchronizer().waitOn("allowed_pre_cas");
    std::thread t1([&] { EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_)); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("allowed_pre_cas");

    // Consume a token on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
    synchronizer().signal("allowed_pre_cas");
    t1.join();
  }
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterImplTest, TokenBucket) {
  initialize(std::chrono::milliseconds(200), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimiterImplTest, TokenBucketMultipleTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));

  // 0 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));

  // 1 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimiterImplTest, TokenBucketMaxTokensGreaterThanTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 1);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterImplTest, TokenBucketStatus) {
  initialize(std::chrono::milliseconds(3000), 2, 2);

  // 2 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(3000), nullptr));
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_EQ(rate_limiter_->maxTokens(route_descriptors_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(route_descriptors_), 1);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(route_descriptors_), 3);

  // 1 -> 0 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_EQ(rate_limiter_->maxTokens(route_descriptors_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(route_descriptors_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(route_descriptors_), 2);

  // 0 -> 0 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(rate_limiter_->requestAllowed(route_descriptors_));
  EXPECT_EQ(rate_limiter_->maxTokens(route_descriptors_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(route_descriptors_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(route_descriptors_), 1);

  // 0 -> 2 tokens
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  fill_timer_->invokeCallback();
  EXPECT_EQ(rate_limiter_->maxTokens(route_descriptors_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(route_descriptors_), 2);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(route_descriptors_), 3);
}

class LocalRateLimiterDescriptorImplTest : public LocalRateLimiterImplTest {
public:
  void initializeWithDescriptor(const std::chrono::milliseconds fill_interval,
                                const uint32_t max_tokens, const uint32_t tokens_per_fill) {

    initializeTimer();

    rate_limiter_ = std::make_shared<LocalRateLimiterImpl>(
        fill_interval, max_tokens, tokens_per_fill, dispatcher_, descriptors_);
  }
  const std::string single_descriptor_config_yaml = R"(
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
    fill_interval: 0.05s
  )";

  // Default token bucket
  std::vector<RateLimit::LocalDescriptor> descriptor_{{{{"foo2", "bar2"}}}};
  std::vector<RateLimit::LocalDescriptor> descriptor2_{{{{"hello", "world"}, {"foo", "bar"}}}};
};

// Verify descriptor rate limit time interval is multiple of token bucket fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, DescriptorRateLimitDivisibleByTokenFillInterval) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 10, 10, "60s"),
                            *descriptors_.Add());

  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(59000), 2, 1, dispatcher_, descriptors_),
      EnvoyException, "local rate descriptor limit is not a multiple of token bucket fill timer");
}

TEST_F(LocalRateLimiterDescriptorImplTest, DuplicateDescriptor) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "0.1s"),
                            *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "0.1s"),
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
    TestUtility::loadFromYaml(
        fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "0.1s"),
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
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
    EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
  }

  // This tests the case in which two allowed checks race.
  {
    initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

    synchronizer().enable();

    // Start a thread and see if we are under limit. This will wait pre-CAS.
    synchronizer().waitOn("allowed_pre_cas");
    std::thread t1([&] { EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_)); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("allowed_pre_cas");

    // Consume a token on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
    synchronizer().signal("allowed_pre_cas");
    t1.join();
  }
}

TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptor2) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(100), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptor) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));

  // 0 -> 1 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));

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
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
}

// Verify token bucket functionality with request per unit > 1.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketMultipleTokensPerFillDescriptor) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 2, 2, "0.1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));

  // 0 -> 2 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));

  // 1 -> 2 tokens
  for (int i = 0; i < 2; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
}

// Verify token bucket functionality with multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDifferentDescriptorDifferentRateLimits) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 1, 1, "1000s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 3, 1);

  // 1 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens for descriptor2_ and 0 only for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
}

// Verify token bucket functionality with multiple descriptors sorted.
TEST_F(LocalRateLimiterDescriptorImplTest,
       TokenBucketDifferentDescriptorDifferentRateLimitsSorted) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 2, 2, "1s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 3, 3);
  std::vector<RateLimit::LocalDescriptor> descriptors{{{{"hello", "world"}, {"foo", "bar"}}},
                                                      {{{"foo2", "bar2"}}}};

  // Descriptors are sorted as descriptor2 < descriptor < global
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptors));
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptors));
  // Request limited by descriptor2 will not consume tokens from descriptor.
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDescriptorStatus) {
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(1000), 2, 2);

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 1);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 3);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 2);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 0 -> 0 tokens
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 1);

  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(1000), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(1000), nullptr));
  fill_timer_->invokeCallback();

  // 0 -> 2 tokens
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 3);
}

// Verify token bucket status of max tokens, remaining tokens and remaining fill interval with
// multiple descriptors.
TEST_F(LocalRateLimiterDescriptorImplTest, TokenBucketDifferentDescriptorStatus) {
  TestUtility::loadFromYaml(multiple_descriptor_config_yaml, *descriptors_.Add());
  TestUtility::loadFromYaml(fmt::format(fmt::runtime(single_descriptor_config_yaml), 2, 2, "3s"),
                            *descriptors_.Add());
  initializeWithDescriptor(std::chrono::milliseconds(50), 2, 1);

  // 2 -> 1 tokens for descriptor_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 1);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 3);

  // 1 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_TRUE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor2_), 1);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor2_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor2_), 0);

  // 0 -> 0 tokens for descriptor_ and descriptor2_
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor2_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor2_), 1);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor2_), 0);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor2_), 0);
  EXPECT_FALSE(rate_limiter_->requestAllowed(descriptor_));
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);

  // 0 -> 1 tokens for descriptor2_
  dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                   Envoy::Event::Dispatcher::RunType::NonBlock);
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
  fill_timer_->invokeCallback();
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor2_), 1);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor2_), 1);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor2_), 0);

  // 0 -> 2 tokens for descriptor_
  for (int i = 0; i < 60; i++) {
    dispatcher_.globalTimeSystem().advanceTimeAndRun(std::chrono::milliseconds(50), dispatcher_,
                                                     Envoy::Event::Dispatcher::RunType::NonBlock);
    EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
    fill_timer_->invokeCallback();
  }
  EXPECT_EQ(rate_limiter_->maxTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingTokens(descriptor_), 2);
  EXPECT_EQ(rate_limiter_->remainingFillInterval(descriptor_), 3);
}

} // Namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
