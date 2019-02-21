#include "extensions/filters/network/kafka/kafka_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

ParseResponse RequestStartParser::parse(const char*& buffer, uint64_t& remaining) {
  request_length_.feed(buffer, remaining);
  if (request_length_.ready()) {
    context_->remaining_request_size_ = request_length_.get();
    return ParseResponse::nextParser(
        std::make_shared<RequestHeaderParser>(parser_resolver_, context_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

ParseResponse RequestHeaderParser::parse(const char*& buffer, uint64_t& remaining) {
  const uint64_t orig_remaining = remaining;
  try {
    context_->remaining_request_size_ -= deserializer_->feed(buffer, remaining);
  } catch (const EnvoyException& e) {
    // unable to compute request header, but we still need to consume rest of request (some of the
    // data might have been consumed)
    const int32_t consumed = static_cast<int32_t>(orig_remaining - remaining);
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

ParseResponse SentinelParser::parse(const char*& buffer, uint64_t& remaining) {
  const size_t min = std::min<size_t>(context_->remaining_request_size_, remaining);
  buffer += min;
  remaining -= min;
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
