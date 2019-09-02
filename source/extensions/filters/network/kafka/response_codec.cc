#include "extensions/filters/network/kafka/response_codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/stack_array.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

void ResponseInitialParserFactory::expectResponse(const int16_t api_key,
                                                  const int16_t api_version) {
  expected_responses_.push({api_key, api_version});
}

ResponseParserSharedPtr
ResponseInitialParserFactory::create(const ResponseParserResolver& parser_resolver) {
  const ExpectedResponseSpec spec = getNextResponseSpec();
  const auto context = std::make_shared<ResponseContext>(spec.first, spec.second);
  return std::make_shared<ResponseHeaderParser>(context, parser_resolver);
}

ExpectedResponseSpec ResponseInitialParserFactory::getNextResponseSpec() {
  if (!expected_responses_.empty()) {
    const ExpectedResponseSpec spec = expected_responses_.front();
    expected_responses_.pop();
    return spec;
  } else {
    // Response data should always be present in expected responses before response is to be parsed.
    throw EnvoyException("attempted to create a response parser while no responses are expected");
  }
}

void ResponseDecoder::expectResponse(const int16_t api_key, const int16_t api_version) {
  factory_->expectResponse(api_key, api_version);
};

ResponseParserSharedPtr ResponseDecoder::createStartParser() {
  return factory_->create(response_parser_resolver_);
}

void ResponseEncoder::encode(const AbstractResponse& message) {
  const uint32_t size = htobe32(message.computeSize());
  output_.add(&size, sizeof(size)); // Encode data length.
  message.encode(output_);          // Encode data.
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
