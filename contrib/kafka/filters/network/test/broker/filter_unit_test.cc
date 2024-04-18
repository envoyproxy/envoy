#include "envoy/event/timer.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "contrib/kafka/filters/network/source/broker/filter.h"
#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/external/requests.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// Mocks.

class MockKafkaMetricsFacade : public KafkaMetricsFacade {
public:
  MOCK_METHOD(void, onMessage, (AbstractRequestSharedPtr));
  MOCK_METHOD(void, onMessage, (AbstractResponseSharedPtr));
  MOCK_METHOD(void, onFailedParse, (RequestParseFailureSharedPtr));
  MOCK_METHOD(void, onFailedParse, (ResponseMetadataSharedPtr));
  MOCK_METHOD(void, onRequestException, ());
  MOCK_METHOD(void, onResponseException, ());
};

using MockKafkaMetricsFacadeSharedPtr = std::shared_ptr<MockKafkaMetricsFacade>;

class MockResponseRewriter : public ResponseRewriter {
public:
  MOCK_METHOD(void, onMessage, (AbstractResponseSharedPtr));
  MOCK_METHOD(void, onFailedParse, (ResponseMetadataSharedPtr));
  MOCK_METHOD(void, process, (Buffer::Instance&));
};

using MockResponseRewriterSharedPtr = std::shared_ptr<MockResponseRewriter>;

class MockResponseDecoder : public ResponseDecoder {
public:
  MockResponseDecoder() : ResponseDecoder{{}} {};
  MOCK_METHOD(void, onData, (Buffer::Instance&));
  MOCK_METHOD(void, expectResponse, (const int32_t, const int16_t, const int16_t));
  MOCK_METHOD(void, reset, ());
};

using MockResponseDecoderSharedPtr = std::shared_ptr<MockResponseDecoder>;

class MockRequestDecoder : public RequestDecoder {
public:
  MockRequestDecoder() : RequestDecoder{{}} {};
  MOCK_METHOD(void, onData, (Buffer::Instance&));
  MOCK_METHOD(void, reset, ());
};

using MockRequestDecoderSharedPtr = std::shared_ptr<MockRequestDecoder>;

class MockTimeSource : public TimeSource {
public:
  MOCK_METHOD(SystemTime, systemTime, ());
  MOCK_METHOD(MonotonicTime, monotonicTime, ());
};

class MockRichRequestMetrics : public RichRequestMetrics {
public:
  MOCK_METHOD(void, onRequest, (const int16_t));
  MOCK_METHOD(void, onUnknownRequest, ());
  MOCK_METHOD(void, onBrokenRequest, ());
};

class MockRichResponseMetrics : public RichResponseMetrics {
public:
  MOCK_METHOD(void, onResponse, (const int16_t, const long long duration));
  MOCK_METHOD(void, onUnknownResponse, ());
  MOCK_METHOD(void, onBrokenResponse, ());
};

class MockRequest : public AbstractRequest {
public:
  MockRequest(const int16_t api_key, const int16_t api_version, const int32_t correlation_id)
      : AbstractRequest{{api_key, api_version, correlation_id, ""}} {};
  uint32_t computeSize() const override { return 0; };
  uint32_t encode(Buffer::Instance&) const override { return 0; };
};

class MockResponse : public AbstractResponse {
public:
  MockResponse(const int16_t api_key, const int32_t correlation_id)
      : AbstractResponse{{api_key, 0, correlation_id}} {};
  uint32_t computeSize() const override { return 0; };
  uint32_t encode(Buffer::Instance&) const override { return 0; };
};

// Tests.

class KafkaBrokerFilterUnitTest : public testing::Test {
protected:
  MockKafkaMetricsFacadeSharedPtr metrics_{std::make_shared<MockKafkaMetricsFacade>()};
  MockResponseRewriterSharedPtr response_rewriter_{std::make_shared<MockResponseRewriter>()};
  MockResponseDecoderSharedPtr response_decoder_{std::make_shared<MockResponseDecoder>()};
  MockRequestDecoderSharedPtr request_decoder_{std::make_shared<MockRequestDecoder>()};

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;

  KafkaBrokerFilter testee_{metrics_, response_rewriter_, response_decoder_, request_decoder_};

  void initialize() {
    testee_.initializeReadFilterCallbacks(filter_callbacks_);
    testee_.onNewConnection();
  }
};

TEST_F(KafkaBrokerFilterUnitTest, ShouldAcceptDataSentByKafkaClient) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_));

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::Continue);
  // Also, request_decoder got invoked.
}

TEST_F(KafkaBrokerFilterUnitTest, ShouldStopIterationIfProcessingDataFromKafkaClientFails) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*request_decoder_, onData(_)).WillOnce(Throw(EnvoyException("boom")));
  EXPECT_CALL(*request_decoder_, reset());
  EXPECT_CALL(*metrics_, onRequestException());

  // when
  initialize();
  const auto result = testee_.onData(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(KafkaBrokerFilterUnitTest, ShouldAcceptDataSentByKafkaBroker) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*response_decoder_, onData(_));
  EXPECT_CALL(*response_rewriter_, process(_));

  // when
  initialize();
  const auto result = testee_.onWrite(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::Continue);
  // Also, request_decoder got invoked.
}

TEST_F(KafkaBrokerFilterUnitTest, ShouldStopIterationIfProcessingDataFromKafkaBrokerFails) {
  // given
  Buffer::OwnedImpl data;
  EXPECT_CALL(*response_decoder_, onData(_)).WillOnce(Throw(EnvoyException("boom")));
  EXPECT_CALL(*response_decoder_, reset());
  EXPECT_CALL(*metrics_, onResponseException());

  // when
  initialize();
  const auto result = testee_.onWrite(data, false);

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

class ForwarderUnitTest : public testing::Test {
protected:
  MockResponseDecoderSharedPtr response_decoder_{std::make_shared<MockResponseDecoder>()};
  Forwarder testee_{*response_decoder_};
};

TEST_F(ForwarderUnitTest, ShouldUpdateResponseDecoderState) {
  // given
  const int16_t api_key = 42;
  const int16_t api_version = 13;
  const int32_t correlation_id = 1234;
  AbstractRequestSharedPtr request =
      std::make_shared<MockRequest>(api_key, api_version, correlation_id);

  EXPECT_CALL(*response_decoder_, expectResponse(correlation_id, api_key, api_version));

  // when
  testee_.onMessage(request);

  // then - response_decoder_ had a new expected response registered.
}

TEST_F(ForwarderUnitTest, ShouldUpdateResponseDecoderStateOnFailedParse) {
  // given
  const int16_t api_key = 42;
  const int16_t api_version = 13;
  const int32_t correlation_id = 1234;
  RequestHeader header = {api_key, api_version, correlation_id, ""};
  RequestParseFailureSharedPtr parse_failure = std::make_shared<RequestParseFailure>(header);

  EXPECT_CALL(*response_decoder_, expectResponse(correlation_id, api_key, api_version));

  // when
  testee_.onFailedParse(parse_failure);

  // then - response_decoder_ had a new expected response registered.
}

class KafkaMetricsFacadeImplUnitTest : public testing::Test {
protected:
  MockTimeSource time_source_;
  std::shared_ptr<MockRichRequestMetrics> request_metrics_ =
      std::make_shared<MockRichRequestMetrics>();
  std::shared_ptr<MockRichResponseMetrics> response_metrics_ =
      std::make_shared<MockRichResponseMetrics>();
  KafkaMetricsFacadeImpl testee_{time_source_, request_metrics_, response_metrics_};
};

TEST_F(KafkaMetricsFacadeImplUnitTest, ShouldRegisterRequest) {
  // given
  const int16_t api_key = 42;
  const int32_t correlation_id = 1234;
  AbstractRequestSharedPtr request = std::make_shared<MockRequest>(api_key, 0, correlation_id);

  EXPECT_CALL(*request_metrics_, onRequest(api_key));

  MonotonicTime time_point{Event::TimeSystem::Milliseconds(1234)};
  EXPECT_CALL(time_source_, monotonicTime()).WillOnce(Return(time_point));

  // when
  testee_.onMessage(request);

  // then
  const auto& request_arrivals = testee_.getRequestArrivalsForTest();
  ASSERT_EQ(request_arrivals.at(correlation_id), time_point);
}

TEST_F(KafkaMetricsFacadeImplUnitTest, ShouldRegisterUnknownRequest) {
  // given
  RequestHeader header = {0, 0, 0, ""};
  RequestParseFailureSharedPtr unknown_request = std::make_shared<RequestParseFailure>(header);

  EXPECT_CALL(*request_metrics_, onUnknownRequest());

  // when
  testee_.onFailedParse(unknown_request);

  // then - request_metrics_ is updated.
}

TEST_F(KafkaMetricsFacadeImplUnitTest, ShouldRegisterResponse) {
  // given
  const int16_t api_key = 42;
  const int32_t correlation_id = 1234;
  AbstractResponseSharedPtr response = std::make_shared<MockResponse>(api_key, correlation_id);

  MonotonicTime request_time_point{Event::TimeSystem::Milliseconds(1234)};
  testee_.getRequestArrivalsForTest()[correlation_id] = request_time_point;

  MonotonicTime response_time_point{Event::TimeSystem::Milliseconds(2345)};

  EXPECT_CALL(*response_metrics_, onResponse(api_key, 1111));
  EXPECT_CALL(time_source_, monotonicTime()).WillOnce(Return(response_time_point));

  // when
  testee_.onMessage(response);

  // then
  const auto& request_arrivals = testee_.getRequestArrivalsForTest();
  ASSERT_EQ(request_arrivals.find(correlation_id), request_arrivals.end());
}

TEST_F(KafkaMetricsFacadeImplUnitTest, ShouldRegisterUnknownResponse) {
  // given
  ResponseMetadataSharedPtr unknown_response = std::make_shared<ResponseMetadata>(0, 0, 0);

  EXPECT_CALL(*response_metrics_, onUnknownResponse());

  // when
  testee_.onFailedParse(unknown_response);

  // then - response_metrics_ is updated.
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
