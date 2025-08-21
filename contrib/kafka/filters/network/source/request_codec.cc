#include "contrib/kafka/filters/network/source/request_codec.h"

#include "source/common/buffer/buffer_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class RequestStartParserFactory : public InitialParserFactory {
  RequestParserSharedPtr create(const RequestParserResolver& parser_resolver) const override {
    return std::make_shared<RequestStartParser>(parser_resolver);
  }
};

const InitialParserFactory& InitialParserFactory::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(RequestStartParserFactory);
}

RequestParserSharedPtr RequestDecoder::createStartParser() {
  return factory_.create(parser_resolver_);
}

void RequestEncoder::encode(const AbstractRequest& message) {
  const uint32_t size = htobe32(message.computeSize());
  output_.add(&size, sizeof(size)); // Encode data length.
  message.encode(output_);          // Encode data.
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
