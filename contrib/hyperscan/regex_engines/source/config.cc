#include "contrib/hyperscan/regex_engines/source/config.h"

#ifndef HYPERSCAN_DISABLED
#include "contrib/hyperscan/regex_engines/source/regex.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

Envoy::Regex::EnginePtr
Config::createEngine(const Protobuf::Message& config,
                     Server::Configuration::ServerFactoryContext& server_factory_context) {
  const auto hyperscan = MessageUtil::downcastAndValidate<
      const envoy::extensions::regex_engines::hyperscan::v3alpha::Hyperscan&>(
      config, server_factory_context.messageValidationVisitor());
#ifdef HYPERSCAN_DISABLED
  throw EnvoyException("X86_64 architecture is required for Hyperscan.");
#else
  return std::make_shared<HyperscanEngine>(server_factory_context.threadLocal());
#endif
}

REGISTER_FACTORY(Config, Envoy::Regex::EngineFactory);

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
