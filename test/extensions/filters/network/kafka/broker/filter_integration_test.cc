#include "common/common/utility.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/kafka/broker/filter.h"
#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/external/responses.h"

#include "test/extensions/filters/network/kafka/buffer_based_test.h"
#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using RequestB = MessageBasedTest<RequestEncoder>;
using ResponseB = MessageBasedTest<ResponseEncoder>;

class KafkaBrokerFilterIntegrationTest : public testing::Test,
                                         protected RequestB,
                                         protected ResponseB {
protected:
  Stats::IsolatedStoreImpl scope_;
  Event::TestRealTimeSystem time_source_;
  KafkaBrokerFilter testee_{scope_, time_source_, "prefix"};

  Network::FilterStatus consumeRequestFromBuffer() {
    return testee_.onData(RequestB::buffer_, false);
  }

  Network::FilterStatus consumeResponseFromBuffer() {
    return testee_.onWrite(ResponseB::buffer_, false);
  }
};

TEST_F(KafkaBrokerFilterIntegrationTest, shouldHandleUnknownRequestAndResponseWithoutBreaking) {
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
  ASSERT_EQ(scope_.counter("kafka.prefix.request.failed_parse").value(), 1);
  ASSERT_EQ(scope_.counter("kafka.prefix.response.failed_parse").value(), 1);
}

TEST_F(KafkaBrokerFilterIntegrationTest, shouldHandleBrokenRequestPayload) {
  // given
  const int16_t api_key = 0;
  const int16_t api_version = 0;
  const int32_t correlation_id = 0;
  const int16_t invalid_nullable_string_length =
      std::numeric_limits<int16_t>::min(); // Breaks parse.

  EncodingContext ec{0};
  ec.encode(static_cast<uint32_t>(sizeof(api_key) + sizeof(api_version) + sizeof(correlation_id) +
                                  sizeof(invalid_nullable_string_length)),
            RequestB::buffer_);
  ec.encode(api_key, RequestB::buffer_);
  ec.encode(api_version, RequestB::buffer_);
  ec.encode(correlation_id, RequestB::buffer_);
  ec.encode(invalid_nullable_string_length, RequestB::buffer_);

  // when
  const Network::FilterStatus result = consumeRequestFromBuffer();

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(KafkaBrokerFilterIntegrationTest, shouldHandleBrokenResponsePayload) {
  // given
  const int32_t correlation_id = 0;
  const int32_t invalid_nullable_array_length =
      std::numeric_limits<int32_t>::min(); // Breaks parse.

  EncodingContext ec{0};
  ec.encode(static_cast<uint32_t>(sizeof(correlation_id) + sizeof(invalid_nullable_array_length)),
            ResponseB::buffer_);
  ec.encode(correlation_id, ResponseB::buffer_);
  ec.encode(invalid_nullable_array_length, ResponseB::buffer_);

  testee_.getResponseDecoderForTest()->expectResponse(0, 0);

  // when
  const Network::FilterStatus result = consumeResponseFromBuffer();

  // then
  ASSERT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(KafkaBrokerFilterIntegrationTest, shouldAbortOnUnregisteredResponse) {
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

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
