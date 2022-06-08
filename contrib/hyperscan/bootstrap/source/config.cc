#include "contrib/hyperscan/bootstrap/source/config.h"

#include "contrib/envoy/extensions/bootstrap/hyperscan/v3alpha/hyperscan.pb.h"
#include "contrib/envoy/extensions/bootstrap/hyperscan/v3alpha/hyperscan.pb.validate.h"

#ifndef HYPERSCAN_DISABLED
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {

Envoy::Regex::CompiledMatcherPtr HyperscanEngine::matcher(const std::string& regex) const {
  std::vector<const char*> expressions{regex.c_str()};
  // Enable leftmost start of match reporting for replaceAll interface.
  std::vector<unsigned int> flags{flag_ | HS_FLAG_SOM_LEFTMOST};
  std::vector<unsigned int> ids{0};

  return std::make_unique<Matching::InputMatchers::Hyperscan::Matcher>(expressions, flags, ids,
                                                                       *tls_);
}

Server::BootstrapExtensionPtr HyperscanEngine::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context) {
  const auto hyperscan_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::hyperscan::v3alpha::Hyperscan&>(
      config, factory_context.messageValidationVisitor());
#ifdef HYPERSCAN_DISABLED
  throw EnvoyException("X86_64 architecture is required for Hyperscan.");
#else
  flag_ |= (HS_FLAG_CASELESS & hyperscan_config.caseless());
  flag_ |= (HS_FLAG_DOTALL & hyperscan_config.dot_all());
  flag_ |= (HS_FLAG_MULTILINE & hyperscan_config.multiline());
  flag_ |= (HS_FLAG_ALLOWEMPTY & hyperscan_config.allow_empty());
  flag_ |= (HS_FLAG_UTF8 & hyperscan_config.utf8());
  flag_ |= (HS_FLAG_UCP & hyperscan_config.utf8() & hyperscan_config.ucp());

  tls_ = factory_context.threadLocal();

  return std::make_unique<Envoy::Regex::EngineExtension>(*this);
#endif
}

ProtobufTypes::MessagePtr HyperscanEngine::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::bootstrap::hyperscan::v3alpha::Hyperscan>();
}

REGISTER_FACTORY(HyperscanEngine, Server::Configuration::BootstrapExtensionFactory);

} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
