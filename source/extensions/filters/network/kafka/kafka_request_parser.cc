#include "extensions/filters/network/kafka/kafka_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const RequestParserResolver& RequestParserResolver::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(RequestParserResolver);
}

ParseResponse RequestStartParser::parse(absl::string_view& data) {
  request_length_.feed(data);
  if (request_length_.ready()) {
    context_->remaining_request_size_ = request_length_.get();
    return ParseResponse::nextParser(
        std::make_shared<RequestHeaderParser>(parser_resolver_, context_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

ParseResponse RequestHeaderParser::parse(absl::string_view& data) {
  const absl::string_view orig_data = data;
  try {
    context_->remaining_request_size_ -= deserializer_->feed(data);
  } catch (const EnvoyException& e) {
    // We were unable to compute the request header, but we still need to consume rest of request
    // (some of the data might have been consumed during this attempt).
    const int32_t consumed = static_cast<int32_t>(orig_data.size() - data.size());
    context_->remaining_request_size_ -= consumed;
    context_->request_header_ = {-1, -1, -1, absl::nullopt};
    return ParseResponse::nextParser(std::make_shared<SentinelParser>(context_));
  }

  if (deserializer_->ready()) {
    RequestHeader request_header = deserializer_->get();
    context_->request_header_ = request_header;
    ParserSharedPtr next_parser = parser_resolver_.createParser(
        request_header.api_key_, request_header.api_version_, context_);
    return ParseResponse::nextParser(next_parser);
  } else {
    return ParseResponse::stillWaiting();
  }
}

ParseResponse SentinelParser::parse(absl::string_view& data) {
  const size_t min = std::min<size_t>(context_->remaining_request_size_, data.size());
  data = {data.data() + min, data.size() - min};
  context_->remaining_request_size_ -= min;
  if (0 == context_->remaining_request_size_) {
    return ParseResponse::parsedMessage(
        std::make_shared<UnknownRequest>(context_->request_header_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
