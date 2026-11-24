#include "source/extensions/local_address_selectors/dynamic_modules/config.h"

#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/local_address_selectors/dynamic_modules/selector.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LocalAddressSelectors {
namespace DynamicModules {

using ProtoConfig = envoy::extensions::local_address_selectors::dynamic_modules::v3::
    DynamicModuleLocalAddressSelector;

absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
DynamicModuleLocalAddressSelectorFactory::createLocalAddressSelector(
    std::vector<Upstream::UpstreamLocalAddress>, std::optional<std::string>) const {
  return absl::InvalidArgumentError(
      "envoy.upstream.local_address_selector.dynamic_modules requires its configuration");
}

absl::StatusOr<Upstream::UpstreamLocalAddressSelectorConstSharedPtr>
DynamicModuleLocalAddressSelectorFactory::createLocalAddressSelectorWithConfig(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    std::vector<Upstream::UpstreamLocalAddress> upstream_local_addresses,
    std::optional<std::string> cluster_name) const {
  const auto& proto_config = MessageUtil::downcastAndValidate<const ProtoConfig&>(
      config, context.messageValidationVisitor());
  const std::string& selector_name = proto_config.selector_name();

  // Build the default selection first, so an invalid bind config is rejected before the module is
  // loaded.
  auto* default_factory =
      Registry::FactoryRegistry<Upstream::UpstreamLocalAddressSelectorFactory>::getFactory(
          "envoy.upstream.local_address_selector.default_local_address_selector");
  ASSERT(default_factory != nullptr);
  auto default_selector = default_factory->createLocalAddressSelector(
      std::move(upstream_local_addresses), cluster_name);
  RETURN_IF_NOT_OK_REF(default_selector.status());

  // No init manager is passed, so only the synchronous local-file and by-name module sources can
  // succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), selector_name, context);
  RETURN_IF_NOT_OK_REF(load_result.status());
  if (load_result->loaded == nullptr) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, selector_name, Extensions::DynamicModules::ModuleLoadErrorStat);
    return absl::InvalidArgumentError(
        "dynamic module local address selectors do not support remote module sources");
  }

  std::string selector_config;
  if (proto_config.has_selector_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.selector_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          context, selector_name, Extensions::DynamicModules::ConfigInitErrorStat);
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to parse local address selector config: ", config_or_error.status().message()));
    }
    selector_config = std::move(config_or_error.value());
  }

  auto selector_module_config = DynamicModuleLocalAddressSelectorConfig::create(
      std::move(load_result->loaded), selector_name, selector_config, cluster_name.value_or(""));
  if (!selector_module_config.ok()) {
    // A missing symbol is a module problem and a rejected config_new is a config problem.
    Extensions::DynamicModules::incrementLoadFailure(
        context, selector_name,
        absl::IsNotFound(selector_module_config.status())
            ? Extensions::DynamicModules::ModuleLoadErrorStat
            : Extensions::DynamicModules::ConfigInitErrorStat);
    return selector_module_config.status();
  }

  return std::make_shared<const DynamicModuleLocalAddressSelector>(
      std::move(selector_module_config.value()), std::move(default_selector.value()));
}

REGISTER_FACTORY(DynamicModuleLocalAddressSelectorFactory,
                 Upstream::UpstreamLocalAddressSelectorFactory);

} // namespace DynamicModules
} // namespace LocalAddressSelectors
} // namespace Extensions
} // namespace Envoy
