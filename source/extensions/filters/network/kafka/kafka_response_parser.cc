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

  const ExpectedResponseSpec spec = getNextResponseSpec();
  context_->api_key_ = spec.first;
  context_->api_version_ = spec.second;
  // At this stage, we have setup the context - we know the response's api key & version, so we can
  // safely create the payload parser.

  auto next_parser = parser_resolver_.createParser(context_);
  return ResponseParseResponse::nextParser(next_parser);
}

ExpectedResponseSpec ResponseHeaderParser::getNextResponseSpec() {
  if (!expected_responses_->empty()) {
    const ExpectedResponseSpec spec = expected_responses_->front();
    expected_responses_->pop();
    return spec;
  } else {
    // Response data should always be present in expected responses before response is to be parsed.
    throw EnvoyException("attempted to create a response parser while no responses are expected");
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
