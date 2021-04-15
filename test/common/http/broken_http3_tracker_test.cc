#include "common/http/broken_http3_tracker.h"

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
class BrokenHttp3TrackerTest : public testing::Test {
public:
  BrokenHttp3TrackerTest()
      : timer_(&dispatcher_), tracker_(dispatcher_) {
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::StrictMock<MockTimer> timer_;
  BrokenHttp3Tracker tracker_;
};

TEST_F(BrokenHttp3TrackerTest, Initialized) {
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBroken) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBrokenRepeatedly) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(true));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBrokenThenExpires) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBrokenWithBackoff) {
  // markBroken will only be called when the time is not enabled.
  EXPECT_CALL(timer_, enabled()).WillRepeatedly(Return(false));

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  timer_.invokeCallback();

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(10*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(20*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(40*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  timer_.invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBrokenThenExpiresThenConfirmedThenBroken) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  timer_.invokeCallback();

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());

  // markConfirmed will have reset the timeout back to the initial value.
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(BrokenHttp3TrackerTest, MarkBrokenThenConfirmed) {
  EXPECT_CALL(timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(timer_, enableTimer(std::chrono::milliseconds(5*60*1000), nullptr)).Times(1);
  tracker_.markHttp3Broken();

  EXPECT_CALL(timer_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(timer_, disableTimer()).Times(1);
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());
}

} // namespace
} // namespace Http
} // namespace Envoy
