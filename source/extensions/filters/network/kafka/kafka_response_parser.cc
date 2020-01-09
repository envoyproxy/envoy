#include "extensions/filters/network/kafka/kafka_response_parser.h"

#include "absl/strings/str_cat.h"

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

  const ExpectedResponseSpec spec = getResponseSpec(context_->correlation_id_);
  context_->api_key_ = spec.first;
  context_->api_version_ = spec.second;
  // At this stage, we have setup the context - we know the response's api key & version, so we can
  // safely create the payload parser.

  auto next_parser = parser_resolver_.createParser(context_);
  return ResponseParseResponse::nextParser(next_parser);
}

ExpectedResponseSpec ResponseHeaderParser::getResponseSpec(const int32_t correlation_id) {
  const auto it = expected_responses_->find(correlation_id);
  if (it != expected_responses_->end()) {
    const auto spec = it->second;
    expected_responses_->erase(it);
    return spec;
  } else {
    // Response data should always be present in expected responses before response is to be parsed.
    throw EnvoyException(
        absl::StrCat("no response metadata registered for correlation_id ", correlation_id));
  }
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
