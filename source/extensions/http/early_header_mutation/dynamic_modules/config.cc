#include "source/extensions/http/early_header_mutation/dynamic_modules/config.h"

#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {

using ProtoDynamicModuleEarlyHeaderMutation = envoy::extensions::http::early_header_mutation::
    dynamic_modules::v3::DynamicModuleEarlyHeaderMutation;

Envoy::Http::EarlyHeaderMutationPtr
Factory::createExtension(const Protobuf::Message& message,
                         Server::Configuration::FactoryContext& context) {
  // HttpConnectionManagerConfig hands the raw typed_config Any straight to createExtension, so it
  // must be unpacked here rather than downcast directly.
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      *Envoy::Protobuf::DynamicCastMessage<const Protobuf::Any>(&message),
      context.messageValidationVisitor(), *this);
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ProtoDynamicModuleEarlyHeaderMutation&>(
          *mptr, context.messageValidationVisitor());

  const auto& mutation_name = proto_config.early_header_mutation_name();
  auto& server_context = context.serverFactoryContext();

  // The server context is passed so a load failure increments module_load_error on a scope that
  // outlives the rejected listener. No init manager is passed, so a remote source that is not
  // already cached is rejected rather than fetched: the extension must be usable for the first
  // request the connection manager serves, and mutate() is const and runs concurrently on every
  // worker thread, so there is nowhere to install a late-arriving module.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), mutation_name, server_context);
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string mutation_config;
  if (proto_config.has_early_header_mutation_config()) {
    auto config_or_error =
        MessageUtil::knownAnyToBytes(proto_config.early_header_mutation_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          server_context, mutation_name, Extensions::DynamicModules::ConfigInitErrorStat);
      throw EnvoyException("Failed to parse early header mutation config: " +
                           std::string(config_or_error.status().message()));
    }
    mutation_config = std::move(config_or_error.value());
  }

  auto extension = newDynamicModuleEarlyHeaderMutation(mutation_name, mutation_config,
                                                       std::move(load_result->loaded));
  if (!extension.ok()) {
    // NotFound means a required ABI symbol was missing, which is a module-level problem.
    const absl::string_view leaf = absl::IsNotFound(extension.status())
                                       ? Extensions::DynamicModules::ModuleLoadErrorStat
                                       : Extensions::DynamicModules::ConfigInitErrorStat;
    Extensions::DynamicModules::incrementLoadFailure(server_context, mutation_name, leaf);
    throw EnvoyException("Failed to create early header mutation config: " +
                         std::string(extension.status().message()));
  }

  return std::move(extension.value());
}

REGISTER_FACTORY(Factory, Envoy::Http::EarlyHeaderMutationFactory);

} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
