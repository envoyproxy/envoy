#include "extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"

#include "test/mocks/event/mocks.h"

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
  void initialize(const std::chrono::milliseconds fill_interval, const uint32_t max_tokens,
                  const uint32_t tokens_per_fill) {

    fill_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*fill_timer_, enableTimer(_, nullptr));
    EXPECT_CALL(*fill_timer_, disableTimer());

    rate_limiter_ = std::make_shared<LocalRateLimiterImpl>(fill_interval, max_tokens,
                                                           tokens_per_fill, dispatcher_);
  }

  Thread::ThreadSynchronizer& synchronizer() { return rate_limiter_->synchronizer_; }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* fill_timer_{};
  std::shared_ptr<LocalRateLimiterImpl> rate_limiter_;
};

// Make sure we fail with a fill rate this is too fast.
TEST_F(LocalRateLimiterImplTest, TooFastFillRate) {
  EXPECT_THROW_WITH_MESSAGE(
      LocalRateLimiterImpl(std::chrono::milliseconds(49), 100, 1, dispatcher_), EnvoyException,
      "local rate limit token bucket fill timer must be >= 50ms");
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
    EXPECT_TRUE(rate_limiter_->requestAllowed());

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(rate_limiter_->requestAllowed());
    EXPECT_FALSE(rate_limiter_->requestAllowed());
  }

  // This tests the case in which two allowed checks race.
  {
    initialize(std::chrono::milliseconds(200), 1, 1);

    synchronizer().enable();

    // Start a thread and see if we are under limit. This will wait pre-CAS.
    synchronizer().waitOn("allowed_pre_cas");
    std::thread t1([&] { EXPECT_FALSE(rate_limiter_->requestAllowed()); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("allowed_pre_cas");

    // Consume a token on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(rate_limiter_->requestAllowed());
    synchronizer().signal("allowed_pre_cas");
    t1.join();
  }
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimiterImplTest, TokenBucket) {
  initialize(std::chrono::milliseconds(200), 1, 1);

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimiterImplTest, TokenBucketMultipleTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 2);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());

  // 0 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 1 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());

  // 1 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimiterImplTest, TokenBucketMaxTokensGreaterThanTokensPerFill) {
  initialize(std::chrono::milliseconds(200), 2, 1);

  // 2 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(rate_limiter_->requestAllowed());
  EXPECT_FALSE(rate_limiter_->requestAllowed());
}

} // Namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
