#pragma once

#include <vector>

#include "envoy/content_parser/factory.h"
#include "envoy/content_parser/parser.h"
#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"
#include "envoy/json/json_object.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace ContentParsers {
namespace Json {

using ProtoRule = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::Rule;
using KeyValuePair =
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::ValueType;
using Selector = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::Selector;

/**
 * Parses JSON content extracts metadata based on JSON path selectors.
 */
class JsonContentParserImpl : public ContentParser::Parser,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  JsonContentParserImpl(
      const envoy::extensions::content_parsers::json::v3::JsonContentParser& config);

  ContentParser::ParseResult parse(absl::string_view data) override;
  std::vector<ContentParser::MetadataAction> getAllDeferredActions() override;

private:
  class Rule {
  public:
    Rule(const ProtoRule& rule, uint32_t stop_processing_after_matches);
    const ProtoRule& rule_;
    std::vector<std::string> selector_path_;
    uint32_t stop_processing_after_matches_;
    size_t match_count_ = 0;
    bool ever_matched_ = false;       // Track if on_present ever fired
    bool selector_not_found_ = false; // Track if selector was ever not found
  };

  // Session-level state (accumulated across parse() calls)
  bool any_parse_error_ = false;

  /**
   * Extract value from JSON object using path.
   */
  absl::StatusOr<Envoy::Json::ValueType>
  extractValueFromJson(const Envoy::Json::ObjectSharedPtr& json_obj,
                       const std::vector<std::string>& path) const;

  /**
   * Convert KeyValuePair to metadata action.
   */
  ContentParser::MetadataAction
  keyValuePairToAction(const KeyValuePair& kv_pair,
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
class JsonContentParserFactory : public ContentParser::ParserFactory {
public:
  JsonContentParserFactory(
      const envoy::extensions::content_parsers::json::v3::JsonContentParser& config);

  ContentParser::ParserPtr createParser() override;
  const std::string& statsPrefix() const override;

private:
  const envoy::extensions::content_parsers::json::v3::JsonContentParser config_;
};

} // namespace Json
} // namespace ContentParsers
} // namespace Extensions
} // namespace Envoy
