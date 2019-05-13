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
  const absl::string_view orig_data = data;
  try {
    context_->remaining_request_size_ -= deserializer_->feed(data);
  } catch (const EnvoyException& e) {
    // We were unable to compute the request header, but we still need to consume rest of request
    // (some of the data might have been consumed during this attempt).
    const int32_t consumed = static_cast<int32_t>(orig_data.size() - data.size());
    context_->remaining_request_size_ -= consumed;
    context_->request_header_ = {-1, -1, -1, absl::nullopt};
    return RequestParseResponse::nextParser(std::make_shared<SentinelParser>(context_));
  }

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

RequestParseResponse SentinelParser::parse(absl::string_view& data) {
  const uint32_t min = std::min<uint32_t>(context_->remaining_request_size_, data.size());
  data = {data.data() + min, data.size() - min};
  context_->remaining_request_size_ -= min;
  if (0 == context_->remaining_request_size_) {
    return RequestParseResponse::parseFailure(
        std::make_shared<RequestParseFailure>(context_->request_header_));
  } else {
    return RequestParseResponse::stillWaiting();
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
