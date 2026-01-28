#include "source/extensions/sse_content_parsers/json/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/sse_content_parsers/json/json_content_parser_impl.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {

SseContentParser::ParserFactoryPtr JsonContentParserConfigFactory::createParserFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& json_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::sse_content_parsers::json::v3::JsonContentParser&>(
      config, context.messageValidationVisitor());

  for (const auto& rule_config : json_config.rules()) {
    const auto& rule = rule_config.rule();
    if (rule.has_on_missing()) {
      const auto& kv_pair = rule.on_missing();
      if (kv_pair.value_type_case() == envoy::extensions::filters::http::json_to_metadata::v3::
                                           JsonToMetadata::KeyValuePair::kValue) {
        if (kv_pair.value().kind_case() == 0) {
          throw EnvoyException("on_missing KeyValuePair with explicit value must have value set");
        }
      }
    }
    if (rule.has_on_error()) {
      const auto& kv_pair = rule.on_error();
      if (kv_pair.value_type_case() == envoy::extensions::filters::http::json_to_metadata::v3::
                                           JsonToMetadata::KeyValuePair::kValue) {
        if (kv_pair.value().kind_case() == 0) {
          throw EnvoyException("on_error KeyValuePair with explicit value must have value set");
        }
      }
    }

    // Require at least one of on_present, on_missing, or on_error to be set
    if (!rule.has_on_present() && !rule.has_on_missing() && !rule.has_on_error()) {
      throw EnvoyException("At least one of on_present, on_missing, or on_error must be specified");
    }
  }

  return std::make_unique<JsonContentParserFactory>(json_config);
}

/**
 * Static registration for the JSON content parser. @see RegisterFactory.
 */
REGISTER_FACTORY(JsonContentParserConfigFactory,
                 SseContentParser::NamedSseContentParserConfigFactory);

} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
