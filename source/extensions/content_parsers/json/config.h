#pragma once

#include "envoy/content_parser/config.h"
#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"
#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace ContentParsers {
namespace Json {

/**
 * Config registration for the JSON content parser.
 */
class JsonContentParserConfigFactory : public ContentParser::NamedContentParserConfigFactory {
public:
  ContentParser::ParserFactoryPtr
  createParserFactory(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::content_parsers::json::v3::JsonContentParser>();
  }

  std::string name() const override { return "envoy.content_parsers.json"; }
};

} // namespace Json
} // namespace ContentParsers
} // namespace Extensions
} // namespace Envoy
