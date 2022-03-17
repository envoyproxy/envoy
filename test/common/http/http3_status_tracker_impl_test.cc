#include "source/common/http/http3_status_tracker_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Event::MockTimer;
using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {

namespace {

class MockHttp3StatusTrackerCallback : public Http3StatusTrackerCallback {
public:
  MOCK_METHOD(void, onHttp3StatusChanged, (const AlternateProtocolsCache::Origin& origin));
};

class Http3StatusTrackerImplTest : public testing::Test {
public:
  Http3StatusTrackerImplTest()
      : timer_(new MockTimer(&dispatcher_)), tracker_(dispatcher_, origin_, callback_) {}

  AlternateProtocolsCache::Origin origin_{"https", "hostname", 1};
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockTimer* timer_; // Owned by tracker_;
  MockHttp3StatusTrackerCallback callback_;
  Http3StatusTrackerImpl tracker_;
};

TEST_F(Http3StatusTrackerImplTest, Initialized) {
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenRepeatedly) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_))).Times(0u);
  tracker_.markHttp3Broken();
  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenExpires) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();

  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  timer_->invokeCallback();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenWithBackoff) {
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_))).Times(8u);

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

TEST_F(Http3StatusTrackerImplTest, MarkBrokenWithBackoffMax) {
  EXPECT_CALL(*timer_, enabled()).WillRepeatedly(Return(false));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_))).Times(20u);

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

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenExpiresThenConfirmedThenBroken) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());

  // markConfirmed will have reset the timeout back to the initial value.
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();

  EXPECT_TRUE(tracker_.isHttp3Broken());
  EXPECT_FALSE(tracker_.isHttp3Confirmed());
}

TEST_F(Http3StatusTrackerImplTest, MarkBrokenThenConfirmed) {
  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5 * 60 * 1000), nullptr));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Broken();
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  timer_->invokeCallback();

  EXPECT_CALL(*timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(callback_, onHttp3StatusChanged(Eq(origin_)));
  tracker_.markHttp3Confirmed();
  EXPECT_FALSE(tracker_.isHttp3Broken());
  EXPECT_TRUE(tracker_.isHttp3Confirmed());
}

} // namespace
} // namespace Http
} // namespace Envoy
