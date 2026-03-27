#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/content_parser/factory.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace ContentParser {

/**
 * Config factory for content parsers. Implementations create ParserFactory instances
 * from protobuf configuration.
 */
class NamedContentParserConfigFactory : public Config::TypedFactory {
public:
  ~NamedContentParserConfigFactory() override = default;

  /**
   * Create a ParserFactory from protobuf configuration.
   * @param config the protobuf configuration for the parser
   * @param context factory context for accessing server resources
   * @return ParserFactoryPtr a parser factory instance
   */
  virtual ParserFactoryPtr
  createParserFactory(const Protobuf::Message& config,
                      Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.content_parsers"; }
};

} // namespace ContentParser
} // namespace Envoy
