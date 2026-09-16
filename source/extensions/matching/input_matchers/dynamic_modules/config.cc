#include "source/extensions/matching/input_matchers/dynamic_modules/config.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

::Envoy::Matcher::InputMatcherFactoryCb
DynamicModuleInputMatcherFactory::createInputMatcherFactoryCb(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  const auto& proto_config = dynamic_cast<const envoy::extensions::matching::input_matchers::
                                              dynamic_modules::v3::DynamicModuleMatcher&>(config);

  const auto& matcher_name = proto_config.matcher_name();
  const auto& module_config = proto_config.dynamic_module_config();
  // Input matchers pass no async callback, so a remote source that is not already cached is
  // rejected rather than fetched.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      module_config, proto_config.matcher_name(), context);
  if (!load_result.ok()) {
    throw EnvoyException(std::string(load_result.status().message()));
  }

  auto dynamic_module = std::move(load_result->loaded);

  // Resolve required symbols. A missing ABI symbol is a module-level problem, so it is counted as
  // module_load_error.
  auto on_config_new = dynamic_module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  if (!on_config_new.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, matcher_name, Extensions::DynamicModules::ModuleLoadErrorStat);
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_new.status().message()));
  }

  auto on_config_destroy = dynamic_module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  if (!on_config_destroy.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, matcher_name, Extensions::DynamicModules::ModuleLoadErrorStat);
    throw EnvoyException("Failed to resolve symbol: " +
                         std::string(on_config_destroy.status().message()));
  }

  auto on_match = dynamic_module->getFunctionPointer<OnMatcherMatchType>(
      "envoy_dynamic_module_on_matcher_match");
  if (!on_match.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, matcher_name, Extensions::DynamicModules::ModuleLoadErrorStat);
    throw EnvoyException("Failed to resolve symbol: " + std::string(on_match.status().message()));
  }

  // Parse the matcher config.
  std::string matcher_config_str;
  if (proto_config.has_matcher_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.matcher_config());
    if (!config_or_error.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          context, matcher_name, Extensions::DynamicModules::ConfigInitErrorStat);
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
    Extensions::DynamicModules::incrementLoadFailure(
        context, matcher_name, Extensions::DynamicModules::ConfigInitErrorStat);
    throw EnvoyException("Failed to initialize dynamic module matcher config");
  }

  // Capture everything needed for the factory callback. The module is shared so it stays loaded.
  auto shared_module =
      std::shared_ptr<Extensions::DynamicModules::DynamicModule>(std::move(dynamic_module));

  // Own the in-module configuration in a shared holder so it is destroyed exactly once through
  // on_matcher_config_destroy, whether the factory callback runs zero, one, or many times. The
  // deleter also holds the module so the destroy hook is never called into an unloaded module.
  std::shared_ptr<const void> shared_config(
      in_module_config, [shared_module, on_config_destroy = on_config_destroy.value()](
                            const void* config) { on_config_destroy(config); });

  return [shared_module, on_match = on_match.value(), shared_config] {
    return std::make_unique<DynamicModuleInputMatcher>(shared_module, on_match, shared_config);
  };
}

REGISTER_FACTORY(DynamicModuleInputMatcherFactory, ::Envoy::Matcher::InputMatcherFactory);

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
