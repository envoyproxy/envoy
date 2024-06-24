/**
 * Tests in this file verify whether Kafka broker filter instance is capable of processing protocol
 * messages properly.
 */

#include "source/common/common/utility.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/test_time.h"

#include "contrib/kafka/filters/network/source/broker/filter.h"
#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/test/broker/mock_filter_config.h"
#include "contrib/kafka/filters/network/test/buffer_based_test.h"
#include "contrib/kafka/filters/network/test/message_utilities.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using RequestB = MessageBasedTest<RequestEncoder>;
using ResponseB = MessageBasedTest<ResponseEncoder>;

// Message size for all kind of broken messages (we are not going to process all the bytes).
constexpr static int32_t BROKEN_MESSAGE_SIZE = std::numeric_limits<int32_t>::max();

class KafkaBrokerFilterProtocolTest : public testing::Test,
                                      protected RequestB,
                                      protected ResponseB {
protected:
  Stats::TestUtil::TestStore store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Event::TestRealTimeSystem time_source_;
  KafkaBrokerFilter testee_{scope_, time_source_,
                            std::make_shared<BrokerFilterConfig>(std::string("prefix"), false,
                                                                 std::vector<RewriteRule>{})};

  Network::FilterStatus consumeRequestFromBuffer() {
    return testee_.onData(RequestB::buffer_, false);
  }

  Network::FilterStatus consumeResponseFromBuffer() {
    return testee_.onWrite(ResponseB::buffer_, false);
  }
};

TEST_F(KafkaBrokerFilterProtocolTest, ShouldHandleUnknownRequestAndResponseWithoutBreaking) {
  // given
  const int16_t unknown_api_key = std::numeric_limits<int16_t>::max();

  const RequestHeader request_header = {unknown_api_key, 0, 0, "client-id"};
  const ProduceRequest request_data = {0, 0, {}};
  const Request<ProduceRequest> produce_request = {request_header, request_data};
  RequestB::putMessageIntoBuffer(produce_request);

  const ResponseMetadata response_metadata = {unknown_api_key, 0, 0};
  const ProduceResponse response_data = {{}};
  const Response<ProduceResponse> produce_response = {response_metadata, response_data};
  ResponseB::putMessageIntoBuffer(produce_response);

  // when
  const Network::FilterStatus result1 = consumeRequestFromBuffer();
  const Network::FilterStatus result2 = consumeResponseFromBuffer();

  // then
  ASSERT_EQ(result1, Network::FilterStatus::Continue);
  ASSERT_EQ(result2, Network::FilterStatus::Continue);
  ASSERT_EQ(store_.counter("kafka.prefix.request.unknown").value(), 1);
  ASSERT_EQ(store_.counter("kafka.prefix.response.unknown").value(), 1);
}

TEST_F(KafkaBrokerFilterProtocolTest, ShouldHandleBrokenRequestPayload) {
  // given

  // Encode broken request into buffer.
  // We will put invalid length of nullable string passed as client-id (length < -1).
  RequestB::putIntoBuffer(BROKEN_MESSAGE_SIZE);
  RequestB::putIntoBuffer(static_cast<int16_t>(0)); // Api key.
  RequestB::putIntoBuffer(static_cast<int16_t>(0)); // Api version.
  RequestB::putIntoBuffer(static_cast<int32_t>(0)); // Correlation-id.
  RequestB::putIntoBuffer(static_cast<int16_t>(std::numeric_limits<int16_t>::min())); // Client-id.

  // when
  const Network::FilterStatus result = consumeRequestFromBuffer();

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
  ASSERT_EQ(testee_.getRequestDecoderForTest()->getCurrentParserForTest(), nullptr);
}

TEST_F(KafkaBrokerFilterProtocolTest, ShouldHandleBrokenResponsePayload) {
  // given

  const int32_t correlation_id = 42;
  // Encode broken response into buffer.
  // Produce response v0 is a nullable array of TopicProduceResponses.
  // Encoding invalid length (< -1) of this nullable array is going to break the parser.
  ResponseB::putIntoBuffer(BROKEN_MESSAGE_SIZE);
  ResponseB::putIntoBuffer(correlation_id); // Correlation-id.
  ResponseB::putIntoBuffer(static_cast<int32_t>(std::numeric_limits<int32_t>::min())); // Array.

  testee_.getResponseDecoderForTest()->expectResponse(correlation_id, 0, 0);

  // when
  const Network::FilterStatus result = consumeResponseFromBuffer();

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
  ASSERT_EQ(testee_.getResponseDecoderForTest()->getCurrentParserForTest(), nullptr);
}

TEST_F(KafkaBrokerFilterProtocolTest, ShouldAbortOnUnregisteredResponse) {
  // given
  const ResponseMetadata response_metadata = {0, 0, 0};
  const ProduceResponse response_data = {{}};
  const Response<ProduceResponse> produce_response = {response_metadata, response_data};
  ResponseB::putMessageIntoBuffer(produce_response);

  // when
  const Network::FilterStatus result = consumeResponseFromBuffer();

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(KafkaBrokerFilterProtocolTest, ShouldProcessMessages) {
  // given
  // For every request/response type & version, put a corresponding request into the buffer.
  for (const AbstractRequestSharedPtr& message : MessageUtilities::makeAllRequests()) {
    RequestB::putMessageIntoBuffer(*message);
  }
  for (const AbstractResponseSharedPtr& message : MessageUtilities::makeAllResponses()) {
    ResponseB::putMessageIntoBuffer(*message);
  }

  // when
  const Network::FilterStatus result1 = consumeRequestFromBuffer();
  const Network::FilterStatus result2 = consumeResponseFromBuffer();

  // then
  ASSERT_EQ(result1, Network::FilterStatus::Continue);
  ASSERT_EQ(result2, Network::FilterStatus::Continue);

  // Also, assert that every message type has been processed properly.
  for (const int16_t i : MessageUtilities::apiKeys()) {
    // We should have received one request per api version.
    const Stats::Counter& request_counter = store_.counter(MessageUtilities::requestMetric(i));
    ASSERT_EQ(request_counter.value(), MessageUtilities::requestApiVersions(i));
    // We should have received one response per api version.
    const Stats::Counter& response_counter = store_.counter(MessageUtilities::responseMetric(i));
    ASSERT_EQ(response_counter.value(), MessageUtilities::responseApiVersions(i));
  }
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
