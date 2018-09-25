#include "extensions/filters/network/kafka/kafka_request.h"

#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/kafka_protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === REQUEST PARSER MAPPING (REQUEST TYPE => PARSER) =========================

GeneratorMap computeGeneratorMap(std::vector<ParserSpec> specs) {
  GeneratorMap result;
  for (auto& spec : specs) {
    std::shared_ptr<std::unordered_map<INT16, GeneratorFunction>> generators = result[spec.api_key_];
    if (!generators) {
      generators = std::make_shared<std::unordered_map<INT16, GeneratorFunction>>();
      result[spec.api_key_] = generators;
    }

    for (INT16 api_version : spec.api_versions_) {
      (*generators)[api_version] = spec.generator_;
    }
  }

  return result;
}

#define PARSER_SPEC(REQUEST_NAME, PARSER_VERSION, ...) \
  ParserSpec{                                                                  \
    RequestType::REQUEST_NAME ,                                                \
    { __VA_ARGS__ },                                                           \
    [](RequestContextSharedPtr arg) -> ParserSharedPtr {                       \
      return std::make_shared< REQUEST_NAME ## Request ## PARSER_VERSION ## Parser>(arg); \
    }                                                                          \
  }

const RequestParserResolver RequestParserResolver::KAFKA_0_11{{
  PARSER_SPEC(Produce, V0, 0, 1, 2),
  PARSER_SPEC(Produce, V3, 3),
  PARSER_SPEC(Fetch, V0, 0, 1, 2),
  PARSER_SPEC(Fetch, V3, 3),
  PARSER_SPEC(Fetch, V4, 4),
  PARSER_SPEC(Fetch, V5, 5),
  PARSER_SPEC(ListOffsets, V0, 0),
  PARSER_SPEC(ListOffsets, V1, 1),
  PARSER_SPEC(ListOffsets, V2, 2),
  PARSER_SPEC(Metadata, V0, 0, 1, 2, 3),
  PARSER_SPEC(Metadata, V4, 4),
  // XXX(adam.kotwasinski) missing request types here
  PARSER_SPEC(OffsetCommit, V0, 0),
  PARSER_SPEC(OffsetCommit, V1, 1),
  PARSER_SPEC(OffsetCommit, V2, 2, 3),
  PARSER_SPEC(OffsetFetch, V0, 0, 1),
  PARSER_SPEC(OffsetFetch, V2, 2, 3),
  PARSER_SPEC(ApiVersions, V0, 0, 1),
}};

ParserSharedPtr RequestParserResolver::createParser(INT16 api_key, INT16 api_version, RequestContextSharedPtr request_ctx) const {
  const auto api_versions_ptr = generators_.find(api_key);
  // unknown api_key
  if (generators_.end() == api_versions_ptr) {
    return std::make_shared<SentinelConsumer>(request_ctx);
  }
  const auto api_versions = api_versions_ptr->second;

  // unknown api_version
  const auto generator = api_versions->find(api_version);
  if (api_versions->end() == generator) {
    return std::make_shared<SentinelConsumer>(request_ctx);
  }

  // found matching parser generator, create parser
  return generator->second(request_ctx);
}

// === HEADER PARSERS ==========================================================

ParseResponse RequestStartParser::parse(const char*& buffer, uint64_t& remaining) {
  buffer_.feed(buffer, remaining);
  if (buffer_.ready()) {
    context_->remaining_request_size_ = buffer_.get();
    return ParseResponse::nextParser(std::make_shared<RequestHeaderParser>(parser_resolver_, context_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

ParseResponse RequestHeaderParser::parse(const char*& buffer, uint64_t& remaining) {
  context_->remaining_request_size_ -= buffer_.feed(buffer, remaining);

  if (buffer_.ready()) {
    RequestHeader request_header = buffer_.get();
    context_->request_header_ = request_header;
    ParserSharedPtr next_parser = parser_resolver_.createParser(
        request_header.api_key_,
        request_header.api_version_,
        context_);
    return ParseResponse::nextParser(next_parser);
  } else {
    return ParseResponse::stillWaiting();
  }
}

// === UNKNOWN REQUEST =========================================================

ParseResponse SentinelConsumer::parse(const char*& buffer, uint64_t& remaining) {
  const size_t min = std::min<size_t>(context_->remaining_request_size_, remaining);
  buffer += min;
  remaining -= min;
  context_->remaining_request_size_ -= min;
  if (0 == context_->remaining_request_size_) {
    return ParseResponse::parsedMessage(std::make_shared<UnknownRequest>(context_->request_header_));
  } else {
    return ParseResponse::stillWaiting();
  }
}

// === REQUEST SERIALIZER ======================================================

size_t RequestSerializer::encode(const Request& request, char *dst) {
  Encoder encoder;
  // encode payload first, then the payload's length
  INT32 data_len = encoder.encode(request, dst + sizeof(INT32));
  INT32 header_len = encoder.encode(data_len, dst);
  return header_len + data_len;
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
