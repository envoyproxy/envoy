#include "extensions/filters/network/kafka/response_codec.h"

#include "test/extensions/filters/network/kafka/buffer_based_test.h"
#include "test/extensions/filters/network/kafka/serialization_utilities.h"
#include "test/mocks/server/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace ResponseCodecIntegrationTest {

class ResponseCodecIntegrationTest : public testing::Test,
                                     public MessageBasedTest<ResponseEncoder> {};

using ResponseCapturingCallback =
    CapturingCallback<ResponseCallback, AbstractResponseSharedPtr, ResponseMetadataSharedPtr>;

// Other response types are tested in (generated) 'response_codec_response_test.cc'.
TEST_F(ResponseCodecIntegrationTest, shouldProduceAbortedMessageOnUnknownData) {
  // given
  const auto callback = std::make_shared<ResponseCapturingCallback>();
  ResponseDecoder testee{{callback}};

  // As real api keys have values below 100, the messages generated in this loop should not be
  // recognized by the codec.
  const int16_t base_api_key = 100;
  std::vector<ResponseMetadata> sent;
  for (int16_t i = 0; i < 1000; ++i) {
    const int16_t api_key = static_cast<int16_t>(base_api_key + i);
    const int16_t api_version = 0;
    const ResponseMetadata metadata = {api_key, api_version, 0};
    const std::vector<unsigned char> data = std::vector<unsigned char>(1024);
    const auto message = Response<std::vector<unsigned char>>{metadata, data};
    putMessageIntoBuffer(message);
    sent.push_back(metadata);
    // We need to register the response, so the parser knows what to expect.
    testee.expectResponse(api_key, api_version);
  }

  // when
  testee.onData(buffer_);

  // then
  ASSERT_EQ(callback->getCapturedMessages().size(), 0);

  const std::vector<ResponseMetadataSharedPtr>& parse_failures = callback->getParseFailures();
  ASSERT_EQ(parse_failures.size(), sent.size());
  for (size_t i = 0; i < parse_failures.size(); ++i) {
    ASSERT_EQ(*(parse_failures[i]), sent[i]);
  }
}

TEST_F(ResponseCodecIntegrationTest, shouldThrowIfAttemptingToParseResponseButNothingIsExpected) {
  // given
  const auto callback = std::make_shared<ResponseCapturingCallback>();
  ResponseDecoder testee{{callback}};

  putGarbageIntoBuffer();

  // when
  bool caught = false;
  try {
    testee.onData(buffer_);
  } catch (EnvoyException& e) {
    caught = true;
  }

  // then
  ASSERT_EQ(caught, true);
}

} // namespace ResponseCodecIntegrationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
