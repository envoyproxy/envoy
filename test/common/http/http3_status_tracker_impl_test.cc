#include "source/common/http/http3_status_tracker_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::MockTimer;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {

namespace {

class Http3StatusTrackerImplTest : public testing::Test {
public:
  Http3StatusTrackerImplTest() : timer_(new MockTimer(&dispatcher_)), tracker_(dispatcher_) {}

  HttpServerPropertiesCache::Origin origin_{"https", "hostname", 1};
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockTimer* timer_; // Owned by tracker_;
  Http3StatusTrackerImpl tracker_;
};

TEST_F(Http3StatusTrackerImplTest, Initialized) {
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
  EXPECT_FALSE(tracker_.hasHttp3FailedRecently());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenRepeatedly) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(true));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenExpires) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
  EXPECT_TRUE(tracker_.hasHttp3FailedRecently());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenWithBackoff) {
  EXPECT_CALL(*timer_, enabled()).WillRepeatedly(Return(false));

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(2 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(4 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(8 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenWithBackoffMax) {
  EXPECT_CALL(*timer_, enabled()).WillRepeatedly(Return(false));

  for (int i = 0; i < 17; ++i) {
    EXPECT_CALL(
        *timer_,
        enableTimer(std::chrono::milliseconds(1 * static_cast<int>(pow(2, i)) * 1000), nullptr));
    tracker_.markHttp3Broken();
    timer_->invokeCallback();
  }

  EXPECT_CALL(
      *timer_,
      enableTimer(std::chrono::milliseconds(1 * static_cast<int>(pow(2, 17)) * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  // Broken period no longer increases.
  EXPECT_CALL(
      *timer_,
      enableTimer(std::chrono::milliseconds(1 * static_cast<int>(pow(2, 17)) * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenExpiresThenConfirmedThenBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.hasHttp3FailedRecently());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());

  // markConfirmed will have reset the timeout back to the initial value.
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.hasHttp3FailedRecently());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenConfirmed) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.hasHttp3FailedRecently());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkFailedRecentlyAndThenBroken) {
  tracker_.markHttp3FailedRecently();
  EXPECT_TRUE(tracker_.hasHttp3FailedRecently());
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1 * 1000), nullptr));
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
  EXPECT_FALSE(tracker_.hasHttp3FailedRecently());
}

} // namespace
} // namespace Http
} // namespace Envoy
