#include "contrib/kafka/filters/network/source/kafka_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const RequestParserResolver& RequestParserResolver::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(RequestParserResolver);
}

RequestParseResponse RequestStartParser::parse(absl::string_view& data) {
  request_length_.feed(data);
  if (request_length_.ready()) {
    context_->remaining_request_size_ = request_length_.get();
    return RequestParseResponse::nextParser(
        std::make_shared<RequestHeaderParser>(parser_resolver_, context_));
  } else {
    return RequestParseResponse::stillWaiting();
  }
}

uint32_t RequestHeaderDeserializer::feed(absl::string_view& data) {
  uint32_t consumed = 0;

  consumed += common_part_deserializer_.feed(data);
  if (common_part_deserializer_.ready()) {
    const auto request_header = common_part_deserializer_.get();
    if (requestUsesTaggedFieldsInHeader(request_header.api_key_, request_header.api_version_)) {
      tagged_fields_present_ = true;
      consumed += tagged_fields_deserializer_.feed(data);
    }
  }

  return consumed;
}

bool RequestHeaderDeserializer::ready() const {
  // Header is only fully parsed after we have processed everything, including tagged fields (if
  // they are present).
  return common_part_deserializer_.ready() &&
         (tagged_fields_present_ ? tagged_fields_deserializer_.ready() : true);
}

RequestHeader RequestHeaderDeserializer::get() const {
  auto result = common_part_deserializer_.get();
  if (tagged_fields_present_) {
    result.tagged_fields_ = tagged_fields_deserializer_.get();
  }
  return result;
}

RequestParseResponse RequestHeaderParser::parse(absl::string_view& data) {
  context_->remaining_request_size_ -= deserializer_->feed(data);
  // One of the two needs must have happened when feeding finishes:
  // - deserializer has consumed all the bytes it needed,
  // - or all the data has been consumed (but deserializer might still need data to be ready).
  ASSERT(deserializer_->ready() || data.empty());
  if (deserializer_->ready()) {
    RequestHeader request_header = deserializer_->get();
    context_->request_header_ = request_header;
    RequestParserSharedPtr next_parser = parser_resolver_.createParser(
        request_header.api_key_, request_header.api_version_, context_);
    return RequestParseResponse::nextParser(next_parser);
  } else {
    return RequestParseResponse::stillWaiting();
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
