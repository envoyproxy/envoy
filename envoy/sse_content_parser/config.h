#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/sse_content_parser/factory.h"

namespace Envoy {
namespace SseContentParser {

/**
 * Config factory for SSE content parsers. Implementations create ParserFactory instances
 * from protobuf configuration.
 */
class NamedSseContentParserConfigFactory : public Config::TypedFactory {
public:
  ~NamedSseContentParserConfigFactory() override = default;

  /**
   * Create a ParserFactory from protobuf configuration.
   * @param config the protobuf configuration for the parser
   * @param context factory context for accessing server resources
   * @return ParserFactoryPtr a parser factory instance
   */
  virtual ParserFactoryPtr
  createParserFactory(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.sse_content_parsers"; }
};

} // namespace SseContentParser
} // namespace Envoy
