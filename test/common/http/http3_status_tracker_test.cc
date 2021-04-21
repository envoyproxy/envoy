#include "common/http/http3_status_tracker.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::MockTimer;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Http {

namespace {
class Http3StatusTrackerTest : public testing::Test {
public:
  Http3StatusTrackerTest()
      : timer_(new StrictMock<MockTimer>(&dispatcher_)), tracker_(dispatcher_) {}

  NiceMock<Event::MockDispatcher> dispatcher_;
  StrictMock<MockTimer>* timer_; // Owned by tracker_;
  Http3StatusTracker tracker_;
};

TEST_F(Http3StatusTrackerTest, Initialized) {
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenRepeatedly) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(true));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenExpires) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenWithBackoff) {
  // markBroken will only be called when the time is not enabled.
  EXPECT_CALL(*timer_, enabled()).WillRepeatedly(Return(false));

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(10 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(20 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenWithBackoffMax) {
  // markBroken will only be called when the time is not enabled.
  EXPECT_CALL(*timer_, enabled()).WillRepeatedly(Return(false));

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(10 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(20 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(80 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(160 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(320 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(640 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1280 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();

  // Broken period no longer increases.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1280 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  timer_->invokeCallback();
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenExpiresThenConfirmedThenBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());

  // markConfirmed will have reset the timeout back to the initial value.
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenConfirmed) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());
}

} // namespace
} // namespace Http
} // namespace Envoy
