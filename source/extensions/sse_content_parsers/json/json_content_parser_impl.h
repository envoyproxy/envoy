#pragma once

#include <vector>

#include "envoy/extensions/sse_content_parsers/json/v3/json_content_parser.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/sse_content_parser/factory.h"
#include "envoy/sse_content_parser/parser.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {

using ProtoRule = envoy::extensions::sse_content_parsers::json::v3::JsonContentParser::Rule;
using MetadataDescriptor =
    envoy::extensions::sse_content_parsers::json::v3::JsonContentParser::MetadataDescriptor;
using ValueType = envoy::extensions::sse_content_parsers::json::v3::JsonContentParser::ValueType;

/**
 * Parses JSON content from SSE events and extracts metadata based on JSON path selectors.
 */
class JsonContentParserImpl : public SseContentParser::Parser,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  JsonContentParserImpl(
      const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser& config);

  // SseContentParser::Parser
  SseContentParser::ParseResult parse(absl::string_view data) override;
  std::vector<SseContentParser::MetadataAction>
  getDeferredActions(size_t rule_index, bool has_error, bool selector_not_found) override;
  size_t numRules() const override { return rules_.size(); }

private:
  struct Rule {
    Rule(const ProtoRule& rule);
    const ProtoRule rule_;
    std::vector<std::string> selector_path_;
  };

  /**
   * Extract value from JSON object using path.
   */
  absl::StatusOr<Envoy::Json::ValueType>
  extractValueFromJson(const Envoy::Json::ObjectSharedPtr& json_obj,
                       const std::vector<std::string>& path) const;

  /**
   * Convert metadata descriptor to action.
   */
  SseContentParser::MetadataAction
  descriptorToAction(const MetadataDescriptor& descriptor,
                     const absl::optional<Envoy::Json::ValueType>& extracted_value) const;

  /**
   * Convert JSON value to Protobuf::Value for metadata.
   */
  Protobuf::Value jsonValueToProtobufValue(const Envoy::Json::ValueType& value,
                                           ValueType type) const;

  std::vector<Rule> rules_;
};

/**
 * Factory for creating JSON content parser instances.
 */
class JsonContentParserFactory : public SseContentParser::ParserFactory {
public:
  JsonContentParserFactory(
      const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser& config);

  // SseContentParser::ParserFactory
  SseContentParser::ParserPtr createParser() override;
  const std::string& statsPrefix() const override;

private:
  const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser config_;
};

} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
