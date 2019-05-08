#include "extensions/filters/network/kafka/request_codec.h"

#include "test/extensions/filters/network/kafka/serialization_utilities.h"
#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace RequestCodecIntegrationTest {

class RequestCodecIntegrationTest : public testing::Test {
protected:
  template <typename T> void putInBuffer(T arg);

  Buffer::OwnedImpl buffer_;
};

// Other request types are tested in (generated) 'request_codec_request_test.cc'.
TEST_F(RequestCodecIntegrationTest, shouldProduceAbortedMessageOnUnknownData) {
  // given
  // As real api keys have values below 100, the messages generated in this loop should not be
  // recognized by the codec.
  const int16_t base_api_key = 100;
  std::vector<RequestHeader> sent_headers;
  for (int16_t i = 0; i < 1000; ++i) {
    const int16_t api_key = static_cast<int16_t>(base_api_key + i);
    const RequestHeader header = {api_key, 0, 0, "client-id"};
    const std::vector<unsigned char> data = std::vector<unsigned char>(1024);
    putInBuffer(Request<std::vector<unsigned char>>{header, data});
    sent_headers.push_back(header);
  }

  const InitialParserFactory& initial_parser_factory = InitialParserFactory::getDefaultInstance();
  const RequestParserResolver& request_parser_resolver =
      RequestParserResolver::getDefaultInstance();
  const CapturingRequestCallbackSharedPtr request_callback =
      std::make_shared<CapturingRequestCallback>();

  RequestDecoder testee{initial_parser_factory, request_parser_resolver, {request_callback}};

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(request_callback->getCaptured().size(), 0);

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

// Helper function.
template <typename T> void RequestCodecIntegrationTest::putInBuffer(T arg) {
  RequestEncoder encoder{buffer_};
  encoder.encode(arg);
}

} // namespace RequestCodecIntegrationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
