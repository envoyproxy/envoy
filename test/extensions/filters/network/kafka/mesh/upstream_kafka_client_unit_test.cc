#include "extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MockKPW : public KPW {
public:
  MOCK_METHOD(RdKafka::ErrorCode, produce,
              (const std::string, int32_t, int, void*, size_t, const void*, size_t, int64_t,
               void*));
  MOCK_METHOD(int, poll, (int));
};

class MockLibRdKafkaUtils : public LibRdKafkaUtils {
public:
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfProperty,
              (RdKafka::Conf&, const std::string&, const std::string&, std::string&), (const));
  MOCK_METHOD(RdKafka::Conf::ConfResult, setConfDeliveryCallback,
              (RdKafka::Conf&, RdKafka::DeliveryReportCb*, std::string&), (const));
  MOCK_METHOD((std::unique_ptr<KPW>), createProducer, (RdKafka::Conf*, std::string& errstr),
              (const));
};

TEST(UpstreamKafkaClientTest, shouldThrowIfBadBootstrapServers) {
  // given
  Event::MockDispatcher dispatcher;
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  RawKafkaProducerConfig config = {{"key1", "value1"}, {"key2", "value2"}};
  MockLibRdKafkaUtils utils;
  EXPECT_CALL(utils, setConfProperty(_, "key1", "value1", _))
      .WillOnce(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(utils, setConfProperty(_, "key2", "value2", _))
      .WillOnce(Return(RdKafka::Conf::CONF_OK));
  EXPECT_CALL(utils, setConfDeliveryCallback(_, _, _)).WillOnce(Return(RdKafka::Conf::CONF_OK));
  std::unique_ptr<MockKPW> producer_ptr = std::make_unique<MockKPW>();
  MockKPW& producer = *producer_ptr;
  EXPECT_CALL(producer, poll(_)).Times(AnyNumber());
  EXPECT_CALL(utils, createProducer(_, _))
      .WillOnce(Return(testing::ByMove(std::move(producer_ptr))));

  // when
  KafkaProducerWrapper testee = {dispatcher, thread_factory, config, utils};

  // then
  // FIXME put something nice here
  ASSERT_NE(&producer, nullptr);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
