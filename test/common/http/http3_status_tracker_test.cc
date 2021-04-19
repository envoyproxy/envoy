#include "common/http/http3_status_tracker.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::MockTimer;
using testing::_;
using testing::AnyNumber;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Http {

namespace {
class Http3StatusTrackerTest : public testing::Test {
public:
  Http3StatusTrackerTest() : timer_(&dispatcher_), tracker_(dispatcher_) {}

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<MockTimer> timer_;
  Http3StatusTracker tracker_;
};

TEST_F(Http3StatusTrackerTest, Initialized) {
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBroken) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenRepeatedly) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(true));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenExpires) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenWithBackoff) {
  // markBroken will only be called when the time is not enabled.
  EXPECT_CALL(timer_, enabled()).WillRepeatedly(Return(false));

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_.invokeCallback();

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(10 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(20 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(40 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenExpiresThenConfirmedThenBroken) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  timer_.invokeCallback();

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());

  // markConfirmed will have reset the timeout back to the initial value.
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerTest, MarkBrokenThenConfirmed) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  tracker_.markHttp3Broken();

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(timer_, disableTimer());
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());
}

} // namespace
} // namespace Http
} // namespace Envoy
