#include "extensions/filters/network/kafka/response_codec.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// Default implementation that just creates ResponseHeaderParser with dependencies provided.
class ResponseInitialParserFactoryImpl : public ResponseInitialParserFactory {
  ResponseParserSharedPtr create(ExpectedResponsesSharedPtr expected_responses,
                                 const ResponseParserResolver& parser_resolver) const override {
    return std::make_shared<ResponseHeaderParser>(expected_responses, parser_resolver);
  }
};

const ResponseInitialParserFactory& ResponseInitialParserFactory::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(ResponseInitialParserFactoryImpl);
}

void ResponseDecoder::expectResponse(const int32_t correlation_id, const int16_t api_key,
                                     const int16_t api_version) {
  (*expected_responses_)[correlation_id] = {api_key, api_version};
};

ResponseParserSharedPtr ResponseDecoder::createStartParser() {
  return factory_.create(expected_responses_, response_parser_resolver_);
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
