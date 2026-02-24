#include "source/extensions/matching/input_matchers/dynamic_modules/config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

::Envoy::Matcher::InputMatcherFactoryCb
DynamicModuleInputMatcherFactory::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& /*context*/) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  const auto& proto_config = dynamic_cast<const envoy::extensions::matching::input_matchers::
                                              dynamic_modules::v3::DynamicModuleMatcher&>(config);

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());

  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module_or_error.status().message()));
  }

  auto dynamic_module = std::move(dynamic_module_or_error.value());

  // Resolve required symbols.
  auto on_config_new = dynamic_module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  if (!on_config_new.ok()) {
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_new.status().message()));
  }

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  if (!on_config_destroy.ok()) {
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_destroy.status().message()));
  }

  auto on_match = dynamic_module->getFunctionPointer<OnMatcherMatchType>(
      "envoy_dynamic_module_on_matcher_match");
  if (!on_match.ok()) {
    throw EnvoyException("Failed to resolve symbol: " + std::string(on_match.status().message()));
  }

  // Parse the matcher config.
  std::string matcher_config_str;
  if (proto_config.has_matcher_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.matcher_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse matcher config: " +
                           std::string(config_or_error.status().message()));
    }
    matcher_config_str = std::move(config_or_error.value());
  }

  // Create the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = proto_config.matcher_name().data(),
                                                     .length = proto_config.matcher_name().size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {.ptr = matcher_config_str.data(),
                                                       .length = matcher_config_str.size()};

  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);
  if (in_module_config == nullptr) {
    throw EnvoyException("Failed to initialize dynamic module matcher config");
  }

  // Capture everything needed for the factory callback. The module is shared so it stays loaded.
  auto shared_module =
      std::shared_ptr<Extensions::DynamicModules::DynamicModule>(std::move(dynamic_module));

  return [shared_module, on_config_destroy = on_config_destroy.value(), on_match = on_match.value(),
          in_module_config] {
    return std::make_unique<DynamicModuleInputMatcher>(shared_module, on_config_destroy, on_match,
                                                       in_module_config);
  };
}

REGISTER_FACTORY(DynamicModuleInputMatcherFactory, ::Envoy::Matcher::InputMatcherFactory);

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
