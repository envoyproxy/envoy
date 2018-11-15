#include "extensions/filters/network/kafka/kafka_request.h"

#include "extensions/filters/network/kafka/kafka_protocol.h"
#include "extensions/filters/network/kafka/messages/offset_commit.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// helper function that generates a map from specs looking like { api_key, api_versions... }
GeneratorMap computeGeneratorMap(const GeneratorMap& original,
                                 const std::vector<ParserSpec> specs) {
  GeneratorMap result{original};
  for (auto& spec : specs) {
    auto& generators = result[spec.api_key_];
    for (int16_t api_version : spec.api_versions_) {
      generators[api_version] = spec.generator_;
    }
  }

  return result;
}

RequestParserResolver::RequestParserResolver(const std::vector<ParserSpec> arg)
    : generators_{computeGeneratorMap({}, arg)} {};

RequestParserResolver::RequestParserResolver(const RequestParserResolver& original,
                                             const std::vector<ParserSpec> arg)
    : generators_{computeGeneratorMap(original.generators_, arg)} {};

#define PARSER_SPEC(REQUEST_NAME, PARSER_VERSION, ...)                                             \
  ParserSpec {                                                                                     \
    RequestType::REQUEST_NAME, {__VA_ARGS__}, [](RequestContextSharedPtr arg) -> ParserSharedPtr { \
      return std::make_shared<REQUEST_NAME##Request##PARSER_VERSION##Parser>(arg);                 \
    }                                                                                              \
  }

const RequestParserResolver RequestParserResolver::KAFKA_0_11{{
    PARSER_SPEC(OffsetCommit, V0, 0), PARSER_SPEC(OffsetCommit, V1, 1),
    // XXX(adam.kotwasinski) missing request types here
}};

const RequestParserResolver RequestParserResolver::KAFKA_1_0{
    RequestParserResolver::KAFKA_0_11,
    {
        // XXX(adam.kotwasinski) missing request types & versions here
    }};

ParserSharedPtr RequestParserResolver::createParser(int16_t api_key, int16_t api_version,
                                                    RequestContextSharedPtr context) const {

  // api_key
  const auto api_versions_ptr = generators_.find(api_key);
  if (generators_.end() == api_versions_ptr) {
    return std::make_shared<SentinelParser>(context);
  }
  const std::unordered_map<int16_t, GeneratorFunction>& api_versions = api_versions_ptr->second;

  // api_version
  const auto generator_ptr = api_versions.find(api_version);
  if (api_versions.end() == generator_ptr) {
    return std::make_shared<SentinelParser>(context);
  }

  // found matching parser generator, create parser
  const GeneratorFunction generator = generator_ptr->second;
  return generator(context);
}

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
  context_->remaining_request_size_ -= deserializer_.feed(buffer, remaining);

  if (deserializer_.ready()) {
    RequestHeader request_header = deserializer_.get();
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
