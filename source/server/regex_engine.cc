#include "source/server/regex_engine.h"

#include "source/common/common/regex.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Server {

Regex::EnginePtr createRegexEngine(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                   ProtobufMessage::ValidationVisitor& validation_visitor,
                                   Configuration::ServerFactoryContext& server_factory_context) {
  Regex::EnginePtr regex_engine;
  if (bootstrap.has_default_regex_engine()) {
    const auto& default_regex_engine = bootstrap.default_regex_engine();
    Regex::EngineFactory& factory =
        Config::Utility::getAndCheckFactory<Regex::EngineFactory>(default_regex_engine);
    auto config = Config::Utility::translateAnyToFactoryConfig(default_regex_engine.typed_config(),
                                                               validation_visitor, factory);
    regex_engine = factory.createEngine(*config, server_factory_context);
  } else {
    regex_engine = std::make_shared<Regex::GoogleReEngine>();
  }
  return regex_engine;
}

} // namespace Server
} // namespace Envoy
