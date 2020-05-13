#include "extensions/filters/network/kafka/request_codec.h"

#include "test/extensions/filters/network/kafka/buffer_based_test.h"
#include "test/extensions/filters/network/kafka/serialization_utilities.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace RequestCodecIntegrationTest {

class RequestCodecIntegrationTest : public testing::Test,
                                    public MessageBasedTest<RequestEncoder> {};

using RequestCapturingCallback =
    CapturingCallback<RequestCallback, AbstractRequestSharedPtr, RequestParseFailureSharedPtr>;

// Other request types are tested in (generated) 'request_codec_request_test.cc'.
TEST_F(RequestCodecIntegrationTest, ShouldProduceAbortedMessageOnUnknownData) {
  // given
  // As real api keys have values below 100, the messages generated in this loop should not be
  // recognized by the codec.
  const int16_t base_api_key = 100;
  std::vector<RequestHeader> sent_headers;
  for (int16_t i = 0; i < 1000; ++i) {
    const int16_t api_key = static_cast<int16_t>(base_api_key + i);
    const RequestHeader header = {api_key, 0, 0, "client-id"};
    const std::vector<unsigned char> data = std::vector<unsigned char>(1024);
    const auto message = Request<std::vector<unsigned char>>{header, data};
    putMessageIntoBuffer(message);
    sent_headers.push_back(header);
  }

  const InitialParserFactory& initial_parser_factory = InitialParserFactory::getDefaultInstance();
  const RequestParserResolver& request_parser_resolver =
      RequestParserResolver::getDefaultInstance();
  const auto request_callback = std::make_shared<RequestCapturingCallback>();

  RequestDecoder testee{initial_parser_factory, request_parser_resolver, {request_callback}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(request_callback->getCapturedMessages().size(), 0);

  const std::vector<RequestParseFailureSharedPtr>& parse_failures =
      request_callback->getParseFailures();
  ASSERT_EQ(parse_failures.size(), sent_headers.size());

  for (size_t i = 0; i < parse_failures.size(); ++i) {
    const std::shared_ptr<RequestParseFailure> failure_data =
        std::dynamic_pointer_cast<RequestParseFailure>(parse_failures[i]);
    ASSERT_NE(failure_data, nullptr);
    ASSERT_EQ(failure_data->request_header_, sent_headers[i]);
  }
}

} // namespace RequestCodecIntegrationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
