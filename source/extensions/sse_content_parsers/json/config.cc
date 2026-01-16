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
      const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser&>(
      config, context.messageValidationVisitor());

  // Validate that on_missing and on_error descriptors have values set
  for (const auto& rule : json_config.rules()) {
    for (const auto& descriptor : rule.on_missing()) {
      if (descriptor.value().kind_case() == 0) {
        throw EnvoyException("on_missing descriptor must have value set");
      }
    }
    for (const auto& descriptor : rule.on_error()) {
      if (descriptor.value().kind_case() == 0) {
        throw EnvoyException("on_error descriptor must have value set");
      }
    }

    // Require at least one of on_present, on_missing, or on_error to be set
    if (rule.on_present().empty() && rule.on_missing().empty() && rule.on_error().empty()) {
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
