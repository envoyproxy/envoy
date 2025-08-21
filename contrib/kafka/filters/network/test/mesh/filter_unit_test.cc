#include "test/mocks/network/mocks.h"

#include "contrib/kafka/filters/network/source/mesh/filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
namespace {

class MockRequestDecoder : public RequestDecoder {
public:
  MockRequestDecoder() : RequestDecoder{{}} {};
  MOCK_METHOD(void, onData, (Buffer::Instance&));
  MOCK_METHOD(void, reset, ());
};

using MockRequestDecoderSharedPtr = std::shared_ptr<MockRequestDecoder>;

class MockInFlightRequest : public InFlightRequest {
public:
  MOCK_METHOD(void, startProcessing, ());
  MOCK_METHOD(bool, finished, (), (const));
  MOCK_METHOD(AbstractResponseSharedPtr, computeAnswer, (), (const));
  MOCK_METHOD(void, abandon, ());
};

using Request = std::shared_ptr<MockInFlightRequest>;

class MockResponse : public AbstractResponse {
public:
  MockResponse() : AbstractResponse{ResponseMetadata{0, 0, 0}} {};
  MOCK_METHOD(uint32_t, computeSize, (), (const));
  MOCK_METHOD(uint32_t, encode, (Buffer::Instance & dst), (const));
};

class FilterUnitTest : public testing::Test {
protected:
  MockRequestDecoderSharedPtr request_decoder_ = std::make_shared<MockRequestDecoder>();
  KafkaMeshFilter testee_{request_decoder_};

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  // Helper: computed response for any kind of request.
  std::shared_ptr<MockResponse> computed_response_ = std::make_shared<NiceMock<MockResponse>>();

  void initialize() {
    testee_.initializeReadFilterCallbacks(filter_callbacks_);
    testee_.onNewConnection();
  }
};

TEST_F(FilterUnitTest, ShouldAcceptDataSentByKafkaClient) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_));

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
  // Also, request_decoder got invoked.
}

TEST_F(FilterUnitTest, ShouldStopIterationIfProcessingDataFromKafkaClientFails) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_)).WillOnce(Throw(EnvoyException("boom")));
  EXPECT_CALL(*request_decoder_, reset());

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(FilterUnitTest, ShouldAcceptAndAbandonRequests) {
  // given
  initialize();
  Request request1 = std::make_shared<Request::element_type>();
  testee_.getRequestsInFlightForTest().push_back(request1);
  EXPECT_CALL(*request1, abandon());
  Request request2 = std::make_shared<Request::element_type>();
  testee_.getRequestsInFlightForTest().push_back(request2);
  EXPECT_CALL(*request2, abandon());

  // when, then - requests get abandoned in destructor.
}

TEST_F(FilterUnitTest, ShouldAcceptAndAbandonRequestsOnConnectionClose) {
  // given
  initialize();
  Request request1 = std::make_shared<Request::element_type>();
  testee_.getRequestsInFlightForTest().push_back(request1);
  EXPECT_CALL(*request1, abandon());
  Request request2 = std::make_shared<Request::element_type>();
  testee_.getRequestsInFlightForTest().push_back(request2);
  EXPECT_CALL(*request2, abandon());

  // when
  testee_.onEvent(Network::ConnectionEvent::LocalClose);

  // then - requests get abandoned (only once).
}

TEST_F(FilterUnitTest, ShouldAcceptAndProcessRequests) {
  // given
  initialize();
  Request request = std::make_shared<Request::element_type>();
  EXPECT_CALL(*request, startProcessing());
  EXPECT_CALL(*request, finished()).WillOnce(Return(true));
  EXPECT_CALL(*request, computeAnswer()).WillOnce(Return(computed_response_));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false));

  // when - 1
  testee_.onRequest(request);

  // then - 1
  ASSERT_EQ(testee_.getRequestsInFlightForTest().size(), 1);

  // when - 2
  testee_.onRequestReadyForAnswer();

  // then - 2
  ASSERT_EQ(testee_.getRequestsInFlightForTest().size(), 0);
}

// This is important - we have two requests, but it is the second one that finishes processing
// first. As Kafka protocol uses sequence numbers, we need to wait until the first finishes.
TEST_F(FilterUnitTest, ShouldAcceptAndProcessRequestsInOrder) {
  // given
  initialize();
  Request request1 = std::make_shared<Request::element_type>();
  Request request2 = std::make_shared<Request::element_type>();
  testee_.getRequestsInFlightForTest().push_back(request1);
  testee_.getRequestsInFlightForTest().push_back(request2);

  EXPECT_CALL(*request1, finished()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*request2, finished()).WillOnce(Return(true));
  EXPECT_CALL(*request1, computeAnswer()).WillOnce(Return(computed_response_));
  EXPECT_CALL(*request2, computeAnswer()).WillOnce(Return(computed_response_));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false)).Times(2);

  // when - 1
  testee_.onRequestReadyForAnswer();

  // then - 1
  ASSERT_EQ(testee_.getRequestsInFlightForTest().size(), 2);

  // when - 2
  testee_.onRequestReadyForAnswer();

  // then - 2
  ASSERT_EQ(testee_.getRequestsInFlightForTest().size(), 0);
}

TEST_F(FilterUnitTest, ShouldDoNothingOnBufferWatermarkEvents) {
  // given
  initialize();

  // when, then - nothing happens.
  testee_.onBelowWriteBufferLowWatermark();
  testee_.onAboveWriteBufferHighWatermark();
}

class MockUpstreamKafkaConfiguration : public UpstreamKafkaConfiguration {
public:
  MOCK_METHOD(void, onData, (Buffer::Instance&));
  MOCK_METHOD(void, reset, ());
  MOCK_METHOD(absl::optional<ClusterConfig>, computeClusterConfigForTopic,
              (const std::string& topic), (const));
  MOCK_METHOD((std::pair<std::string, int32_t>), getAdvertisedAddress, (), (const));
};

class MockUpstreamKafkaFacade : public UpstreamKafkaFacade {
public:
  MOCK_METHOD(KafkaProducer&, getProducerForTopic, (const std::string&));
};

class MockRecordCallbackProcessor : public RecordCallbackProcessor {
public:
  MOCK_METHOD(void, processCallback, (const RecordCbSharedPtr&));
  MOCK_METHOD(void, removeCallback, (const RecordCbSharedPtr&));
};

TEST(Filter, ShouldBeConstructable) {
  // given
  MockUpstreamKafkaConfiguration configuration;
  MockUpstreamKafkaFacade upstream_kafka_facade;
  MockRecordCallbackProcessor record_callback_processor;

  // when
  KafkaMeshFilter filter =
      KafkaMeshFilter(configuration, upstream_kafka_facade, record_callback_processor);

  // then - no exceptions.
}

} // namespace
} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
