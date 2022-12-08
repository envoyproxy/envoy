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
using testing::Exactly;
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

// Utility class: thread safe "store" for values with blocking access.
template <typename T> class Holder {
private:
  mutable absl::Mutex mutex_;
  bool data_set_ ABSL_GUARDED_BY(mutex_) = false;
  T data_ ABSL_GUARDED_BY(mutex_);

public:
  // Stores the first value put inside.
  void put(T arg) {
    absl::MutexLock lock{&mutex_};
    if (!data_set_) {
      data_set_ = true;
      data_ = arg;
    }
  }

  // Blocks until some value appears, and returns it.
  T await() const {
    absl::MutexLock lock{&mutex_, absl::Condition(this, &Holder::hasDataSet)};
    return data_;
  }

private:
  bool hasDataSet() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return data_set_; }
};

// Expected behaviour: if there is interest, then poll for records, and pass them to processor.
TEST_F(UpstreamKafkaConsumerTest, ShouldReceiveRecordsFromKafkaConsumer) {
  // given
  setupConstructorExpectations();

  EXPECT_CALL(record_processor_, waitUntilInterest(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));

  EXPECT_CALL(consumer_, consume(_))
      .Times(AnyNumber())
      .WillRepeatedly([]() -> RdKafkaMessageRawPtr {
        return new NiceMock<MockKafkaMessage>(); // Will be freed by the worker thread.
      });

  Holder<InboundRecordSharedPtr> holder;
  EXPECT_CALL(record_processor_, receive(_))
      .Times(AtLeast(1))
      .WillRepeatedly(Invoke(&holder, &Holder<InboundRecordSharedPtr>::put));

  // when
  const auto testee = makeTestee();

  // then - record processor got notified with a message.
  InboundRecordSharedPtr record = holder.await();
  ASSERT_TRUE(nullptr != record);
}

// Rich consumer's constructor starts a worker thread.
// We are going to wait for at least one request for interest, and then destroy the consumer.
TEST_F(UpstreamKafkaConsumerTest, ShouldCheckInterestUntilShutdown) {
  // given
  setupConstructorExpectations();

  // Because there will be no interest, there won't be any reading from upstream.
  EXPECT_CALL(consumer_, consume(_)).Times(Exactly(0));

  // Mutex for conditional critical section - at least one processor call has been invoked.
  absl::Mutex mt;
  int interest_calls = 0;

  EXPECT_CALL(record_processor_, waitUntilInterest(_, _))
      .Times(AtLeast(1))
      .WillRepeatedly([&mt, &interest_calls]() {
        absl::MutexLock lock(&mt);
        interest_calls++;
        return false; // There is no interest, but keep churning until shutdown.
      });

  // when
  const auto testee = makeTestee();

  // then - the below conditional lock is acquired,
  // what means that the worker thread interacted with the request processor.
  const auto at_least_one_interest_call_has_occurred = [&interest_calls]() -> bool {
    return interest_calls > 0;
  };
  absl::MutexLock lock(&mt, absl::Condition(&at_least_one_interest_call_has_occurred));
}

// Rich consumer's constructor starts a worker thread.
// We are going to wait for at least one invocation of consumer 'consume', so we are confident that
// it does polling. Then we are going to destroy the testee, and expect the thread to finish.
TEST_F(UpstreamKafkaConsumerTest, ShouldConsumeUntilShutdown) {
  // given
  setupConstructorExpectations();

  // In this scenario there are requests waiting for data, so we want to consume from upstream.
  EXPECT_CALL(record_processor_, waitUntilInterest(_, _))
      .Times(AnyNumber())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(record_processor_, receive(_)).Times(AtLeast(1));

  // Mutex for conditional critical section - at least one consumer 'consume' call has been invoked.
  absl::Mutex mt;
  int consume_calls = 0;
  EXPECT_CALL(consumer_, consume(_)).Times(AtLeast(1)).WillRepeatedly([&mt, &consume_calls]() {
    absl::MutexLock lock(&mt);
    consume_calls++;
    return new NiceMock<MockKafkaMessage>(); // Will be freed by the testee in its destructor.
  });

  // when
  const auto testee = makeTestee();

  // then - the below conditional lock is acquired,
  // what means that the worker thread interacted with underlying Kafka consumer.
  const auto at_least_one_consume_call_has_occurred = [&consume_calls]() -> bool {
    return consume_calls > 0;
  };
  absl::MutexLock lock(&mt, absl::Condition(&at_least_one_consume_call_has_occurred));
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
