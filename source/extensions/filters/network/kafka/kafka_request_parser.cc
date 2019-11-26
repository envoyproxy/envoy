#include "extensions/filters/network/kafka/kafka_request_parser.h"

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
