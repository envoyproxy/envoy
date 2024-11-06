//#include "envoy/upstream/outlier_detection.h"

#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

using namespace testing;

// HTTP codes
TEST(MonitorBaseTest, HTTPCodeError) {
  HttpCode http(200);

  ASSERT_EQ(200, http.code());
}

TEST(MonitorBaseTest, HTTPCodeErrorBucket) {
  HTTPCodesBucket bucket(400, 404);
  ASSERT_TRUE(bucket.matchType(HttpCode(200)));
  ASSERT_FALSE(bucket.match(HttpCode(200)));
  ASSERT_FALSE(bucket.match(HttpCode(500)));
  ASSERT_TRUE(bucket.match(HttpCode(403)));
  // test at the edges of the bucket.
  ASSERT_TRUE(bucket.match(HttpCode(400)));
  ASSERT_TRUE(bucket.match(HttpCode(404)));

  // Http-codes bucket should not "catch" other types.
  ASSERT_FALSE(bucket.matchType(LocalOriginEvent(Result::LocalOriginTimeout)));
}

// Locally originated events.
TEST(MonitorBaseTest, LocalOriginError) {
  LocalOriginEvent local_origin_event(Result::ExtOriginRequestSuccess);
  ASSERT_EQ(local_origin_event.result(), Result::ExtOriginRequestSuccess);
}

TEST(MonitorBaseTest, LocalOriginErrorBucket) {
  // Local origin bucket should "catch" all events except ones indicating
  // success.
  LocalOriginEventsBucket bucket;

  // Check that event and bucket have matching types.
  ASSERT_TRUE(bucket.matchType(LocalOriginEvent(Result::ExtOriginRequestSuccess)));

  ASSERT_FALSE(bucket.match(LocalOriginEvent(Result::ExtOriginRequestSuccess)));
  ASSERT_FALSE(bucket.match(LocalOriginEvent(Result::LocalOriginConnectSuccessFinal)));
  ASSERT_TRUE(bucket.match(LocalOriginEvent(Result::LocalOriginTimeout)));
  ASSERT_TRUE(bucket.match(LocalOriginEvent(Result::LocalOriginConnectFailed)));
  ASSERT_TRUE(bucket.match(LocalOriginEvent(Result::ExtOriginRequestFailed)));

  // The bucket should not match other error types.
  ASSERT_FALSE(bucket.matchType(HttpCode(200)));
}

// Test monitor's logic of matching error types and calling appropriate methods.
class MockMonitor : public ExtMonitorBase {
public:
  MockMonitor(ExtMonitorConfigSharedPtr config) : ExtMonitorBase(config) {}
  MOCK_METHOD(bool, onError, ());
  MOCK_METHOD(void, onSuccess, ());
  MOCK_METHOD(void, onReset, ());
};

class TestBucket : public TypedErrorsBucket<Upstream::Outlier::HttpCode> {
public:
  TestBucket() = default;
};

class MockBucket : public TestBucket {
public:
  MockBucket() = default;
  MOCK_METHOD(bool, match, (const ExtResult&), (const));
};

class MonitorTest : public testing::Test {
protected:
  void SetUp() override {
    config_ = std::make_shared<ExtMonitorConfig>(std::string(monitor_name_), enforcing_);
    monitor_ = std::make_unique<MockMonitor>(config_);
  }

  MockBucket* addBucket() {
    auto bucket = std::make_unique<MockBucket>();
    // Store the underlying pointer to the bucket.
    auto bucket_raw_ptr = bucket.get();

    // Add bucket to the monitor.
    config_->addErrorBucket(std::move(bucket));
    return bucket_raw_ptr;
  }

  void addBucket1() { bucket1_ = addBucket(); }
  void addBucket2() { bucket2_ = addBucket(); }

  static constexpr absl::string_view monitor_name_ = "mock-monitor";
  // Pick a easy to recognize number for enforcing.
  static constexpr uint32_t enforcing_ = 43;
  MockBucket* bucket1_;
  MockBucket* bucket2_;
  ExtMonitorConfigSharedPtr config_;
  std::unique_ptr<MockMonitor> monitor_;
};

TEST_F(MonitorTest, NoBuckets) { monitor_->putResult(HttpCode(200)); }

TEST_F(MonitorTest, SingleBucketNotMatchingType) {
  addBucket1();

  // None of the follow-up routines should be called.
  EXPECT_CALL(*monitor_, onSuccess).Times(0);
  EXPECT_CALL(*monitor_, onError).Times(0);
  EXPECT_CALL(*monitor_, onReset).Times(0);

  // Monitor is interested only in Results of ExtResultType::TEST
  // type and here ExtResultType::LOCAL_ORIGIN is sent.
  monitor_->putResult(LocalOriginEvent(Result::ExtOriginRequestSuccess));
}

// Type of the reported "result" matches the type of the
// bucket, but the bucket does not catch the reported result
// and is therefore treated as positive result.
TEST_F(MonitorTest, SingleBucketNotMatchingResult) {
  addBucket1();
  // "matches" checks if the reported error matches the bucket.
  // If "matches" returns false, the result is considered an non-error
  // and monitor's "onSuccess" is called. Depending on type and
  // implementation of the monitor, it may decrease or reset internal
  // monitor's counters.
  bool callback_called = false;
  monitor_->setExtMonitorCallback(
      [&callback_called](const ExtMonitor*) { callback_called = true; });
  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(false));
  EXPECT_CALL(*monitor_, onSuccess);

  monitor_->putResult(HttpCode(200));

  ASSERT_FALSE(callback_called);
}

TEST_F(MonitorTest, SingleBucketMatchingResultNotTripped) {
  addBucket1();
  // "matches" checks if the reported error matches the bucket.
  // If "matches" returns true, the result is considered an error
  // and monitor's "onError" is called. Depending on type and
  // implementation of the monitor, it may increase internal
  // monitor's counters and "trip" the monitor.
  bool callback_called = false;
  monitor_->setExtMonitorCallback(
      [&callback_called](const ExtMonitor*) { callback_called = true; });
  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(true));
  // Return that the monitor has not been tripped.
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(false));

  monitor_->putResult(HttpCode(200));

  // Callback has not been called, because onError returned false,
  // meaning that monitor has not tripped yet.
  ASSERT_FALSE(callback_called);
}

TEST_F(MonitorTest, SingleBucketMatchingResultTripped) {
  addBucket1();
  // "matches" checks if the reported error matches the bucket.
  // If "matches" returns true, the result is considered an error
  // and monitor's "onError" is called. Depending on type and
  // implementation of the monitor, it may increase internal
  // monitor's counters and "trip" the monitor.
  bool callback_called = false;
  monitor_->setExtMonitorCallback([&callback_called](const ExtMonitor* monitor) {
    callback_called = true;
    ASSERT_EQ(monitor->name(), monitor_name_);
    ASSERT_EQ(monitor->enforce(), enforcing_);
  });
  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(true));
  // Return that the monitor has been tripped.
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(true));
  // After tripping, the monitor is reset
  EXPECT_CALL(*monitor_, onReset);

  monitor_->putResult(HttpCode(200));

  // Callback has been called, because onError returned true,
  // meaning that monitor has tripped.
  ASSERT_TRUE(callback_called);
}

TEST_F(MonitorTest, TwoBucketsNotMatching) {
  addBucket1();
  addBucket2();

  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(false));
  EXPECT_CALL(*bucket2_, match(_)).WillOnce(Return(false));
  EXPECT_CALL(*monitor_, onSuccess);

  monitor_->putResult(HttpCode(200));
}

TEST_F(MonitorTest, TwoBucketsFirstMatching) {
  addBucket1();
  addBucket2();

  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(true));
  // Matching the second bucket should be skipped.
  EXPECT_CALL(*bucket2_, match(_)).Times(0);
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(false));

  monitor_->putResult(HttpCode(200));
}

TEST_F(MonitorTest, TwoBucketsSecondMatching) {
  addBucket1();
  addBucket2();

  bool callback_called = false;
  monitor_->setExtMonitorCallback([&callback_called](const ExtMonitor* monitor) {
    callback_called = true;
    ASSERT_EQ(monitor->name(), monitor_name_);
    ASSERT_EQ(monitor->enforce(), enforcing_);
  });
  EXPECT_CALL(*bucket1_, match(_)).WillOnce(Return(false));
  EXPECT_CALL(*bucket2_, match(_)).WillOnce(Return(true));
  EXPECT_CALL(*monitor_, onError).WillOnce(Return(true));
  // After tripping, the monitor is reset
  EXPECT_CALL(*monitor_, onReset);

  monitor_->putResult(HttpCode(200));

  // Callback has been called, because onError returned true,
  // meaning that monitor has tripped.
  ASSERT_TRUE(callback_called);
}

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
