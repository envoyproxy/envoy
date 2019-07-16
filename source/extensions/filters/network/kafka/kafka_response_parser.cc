#include "extensions/filters/network/kafka/kafka_response_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const ResponseParserResolver& ResponseParserResolver::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(ResponseParserResolver);
}

ResponseParseResponse ResponseHeaderParser::parse(absl::string_view& data) {
  length_deserializer_.feed(data);
  if (!length_deserializer_.ready()) {
    return ResponseParseResponse::stillWaiting();
  }

  correlation_id_deserializer_.feed(data);
  if (!correlation_id_deserializer_.ready()) {
    return ResponseParseResponse::stillWaiting();
  }

  context_->remaining_response_size_ = length_deserializer_.get();
  context_->remaining_response_size_ -= sizeof(context_->correlation_id_);
  context_->correlation_id_ = correlation_id_deserializer_.get();

  auto next_parser = parser_resolver_.createParser(context_);
  return ResponseParseResponse::nextParser(next_parser);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
