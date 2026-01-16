#pragma once

#include "envoy/extensions/sse_content_parsers/json/v3/json_content_parser.pb.h"
#include "envoy/extensions/sse_content_parsers/json/v3/json_content_parser.pb.validate.h"
#include "envoy/sse_content_parser/config.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {

/**
 * Config registration for the JSON content parser for SSE.
 */
class JsonContentParserConfigFactory : public SseContentParser::NamedSseContentParserConfigFactory {
public:
  SseContentParser::ParserFactoryPtr
  createParserFactory(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::sse_content_parsers::json::v3::JsonContentParser>();
  }

  std::string name() const override { return "envoy.sse_content_parsers.json"; }
};

} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
