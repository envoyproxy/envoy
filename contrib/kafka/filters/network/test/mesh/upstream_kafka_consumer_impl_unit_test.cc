#include <functional>

#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/mutex.h"
#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer_impl.h"
#include "contrib/kafka/filters/network/test/mesh/kafka_mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNull;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MockInboundRecordProcessor : public InboundRecordProcessor {
public:
  MOCK_METHOD(void, receive, (InboundRecordSharedPtr), ());
  MOCK_METHOD(bool, waitUntilInterest, (const std::string&, const int32_t), (const));
};

class UpstreamKafkaConsumerTest : public testing::Test {
protected:
  Thread::ThreadFactory& thread_factory_ = Thread::threadFactoryForTest();
  MockLibRdKafkaUtils kafka_utils_;
  RawKafkaConfig config_ = {{"key1", "value1"}, {"key2", "value2"}};

  std::unique_ptr<MockKafkaConsumer> consumer_ptr_ = std::make_unique<MockKafkaConsumer>();
  MockKafkaConsumer& consumer_ = *consumer_ptr_;

  MockInboundRecordProcessor record_processor_;

  // Helper method - allows creation of RichKafkaConsumer without problems.
  void setupConstructorExpectations() {
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key1", "value1", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, setConfProperty(_, "key2", "value2", _))
        .WillOnce(Return(RdKafka::Conf::CONF_OK));
    EXPECT_CALL(kafka_utils_, assignConsumerPartitions(_, "topic", 42));

    // These two methods get called in the destructor.
    EXPECT_CALL(consumer_, unassign());
    EXPECT_CALL(consumer_, close());

    EXPECT_CALL(kafka_utils_, createConsumer(_, _))
        .WillOnce(Return(ByMove(std::move(consumer_ptr_))));
  }

  // Helper method - creates the testee with all the mocks injected.
  KafkaConsumerPtr makeTestee() {
    return std::make_unique<RichKafkaConsumer>(record_processor_, thread_factory_, "topic", 42,
                                               config_, kafka_utils_);
  }
};

// This handles situations when users pass bad config to raw consumer_.
TEST_F(UpstreamKafkaConsumerTest, ShouldThrowIfSettingPropertiesFails) {
  // given
  EXPECT_CALL(kafka_utils_, setConfProperty(_, _, _, _))
      .WillOnce(Return(RdKafka::Conf::CONF_INVALID));

  // when, then - exception gets thrown during construction.
  EXPECT_THROW(makeTestee(), EnvoyException);
}

TEST_F(UpstreamKafkaConsumerTest, ShouldThrowIfRawConsumerConstructionFails) {
  // given
  EXPECT_CALL(kafka_utils_, setConfProperty(_, _, _, _))
      .WillRepeatedly(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(kafka_utils_, createConsumer(_, _)).WillOnce(ReturnNull());

  // when, then - exception gets thrown during construction.
  EXPECT_THROW(makeTestee(), EnvoyException);
}

// Utility class for counting invocations / capturing invocation data.
template <typename T> class Tracker {
private:
  mutable absl::Mutex mutex_;
  int invocation_count_ ABSL_GUARDED_BY(mutex_) = 0;
  T data_ ABSL_GUARDED_BY(mutex_);

public:
  // Stores the first value put inside.
  void registerInvocation(const T& arg) {
    absl::MutexLock lock{&mutex_};
    if (0 == invocation_count_) {
      data_ = arg;
    }
    invocation_count_++;
  }

  // Blocks until some value appears, and returns it.
  T awaitFirstInvocation() const {
    const auto cond = std::bind(&Tracker::hasInvocations, this, 1);
    absl::MutexLock lock{&mutex_, absl::Condition(&cond)};
    return data_;
  }

  // Blocks until N invocations have happened.
  void awaitInvocations(const int n) const {
    const auto cond = std::bind(&Tracker::hasInvocations, this, n);
    absl::MutexLock lock{&mutex_, absl::Condition(&cond)};
  }

private:
  bool hasInvocations(const int n) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return invocation_count_ >= n;
  }
};

// Utility method: creates a Kafka message with given error code.
// Results will be freed by the worker thread.
static RdKafkaMessageRawPtr makeMessage(const RdKafka::ErrorCode error_code) {
  const auto result = new NiceMock<MockKafkaMessage>();
  ON_CALL(*result, err()).WillByDefault(Return(error_code));
  return result;
}

// Expected behaviour: if there is interest, then poll for records, and pass them to processor.
TEST_F(UpstreamKafkaConsumerTest, ShouldReceiveRecordsFromKafkaConsumer) {
  // given
  setupConstructorExpectations();

  EXPECT_CALL(record_processor_, waitUntilInterest(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(consumer_, consume(_)).Times(AnyNumber()).WillRepeatedly([]() {
    return makeMessage(RdKafka::ERR_NO_ERROR);
  });

  Tracker<InboundRecordSharedPtr> tracker;
  EXPECT_CALL(record_processor_, receive(_))
      .Times(AtLeast(1))
      .WillRepeatedly(Invoke(&tracker, &Tracker<InboundRecordSharedPtr>::registerInvocation));

  // when
  const auto testee = makeTestee();

  // then - record processor got notified with a message.
  const InboundRecordSharedPtr record = tracker.awaitFirstInvocation();
  ASSERT_NE(record, nullptr);
}

// Expected behaviour: if there is no data, we send nothing to processor.
TEST_F(UpstreamKafkaConsumerTest, ShouldHandleNoDataGracefully) {
  // given
  setupConstructorExpectations();

  Tracker<void*> tracker;
  EXPECT_CALL(record_processor_, waitUntilInterest(_, _))
      .Times(AnyNumber())
      .WillRepeatedly([&tracker]() {
        tracker.registerInvocation(nullptr);
        return true;
      });

  EXPECT_CALL(consumer_, consume(_)).Times(AnyNumber()).WillRepeatedly([]() {
    return makeMessage(RdKafka::ERR__TIMED_OUT);
  });

  // We do not expect to receive any meaningful records.
  EXPECT_CALL(record_processor_, receive(_)).Times(0);

  // when
  const auto testee = makeTestee();

  // then - we have run a few loops, but the processor was never interacted with.
  tracker.awaitInvocations(13);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
