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

ResponseParseResponse SentinelResponseParser::parse(absl::string_view& data) {
  const uint32_t min = std::min<uint32_t>(context_->remaining_response_size_, data.size());
  data = {data.data() + min, data.size() - min};
  context_->remaining_response_size_ -= min;
  if (0 == context_->remaining_response_size_) {
    const auto failure_data = std::make_shared<ResponseMetadata>(
        context_->api_key_, context_->api_version_, context_->correlation_id_);
    return ResponseParseResponse::parseFailure(failure_data);
  } else {
    return ResponseParseResponse::stillWaiting();
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
