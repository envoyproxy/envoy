#include "source/extensions/http/ai_filters/dynamic_modules/config.h"

#include <string>
#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/http/ai_filters/dynamic_modules/ai_filter.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterFactoryCb;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;

absl::StatusOr<AiFilterFactoryCb> DynamicModuleAiFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope&) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::ai_filters::dynamic_modules::v3::DynamicModuleAiFilter&>(
      config, context.messageValidationVisitor());
  const std::string& filter_name = proto.filter_name();

  // Without an init manager a remote module that is not cached is rejected rather than fetched:
  // the AI Protocol Manager builds its filters once, with nowhere to install a late module.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto.dynamic_module_config(), filter_name, context);
  if (!load_result.ok()) {
    return load_result.status();
  }

  std::string filter_config;
  if (proto.has_filter_config()) {
    absl::StatusOr<std::string> bytes = MessageUtil::knownAnyToBytes(proto.filter_config());
    if (!bytes.ok()) {
      Extensions::DynamicModules::incrementLoadFailure(
          context, filter_name, Extensions::DynamicModules::ConfigInitErrorStat);
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to parse AI filter config: ", bytes.status().message()));
    }
    filter_config = *std::move(bytes);
  }

  absl::StatusOr<DynamicModuleAiFilterConfigSharedPtr> filter_config_or =
      DynamicModuleAiFilterConfig::create(filter_name, filter_config,
                                          std::move(load_result->loaded),
                                          context.mainThreadDispatcher());
  if (!filter_config_or.ok()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, filter_name,
        absl::IsNotFound(filter_config_or.status())
            ? Extensions::DynamicModules::ModuleLoadErrorStat
            : Extensions::DynamicModules::ConfigInitErrorStat);
    return absl::Status(
        filter_config_or.status().code(),
        absl::StrCat("Failed to create AI filter config: ", filter_config_or.status().message()));
  }

  return [filter_config = *std::move(filter_config_or)](
             const AiFilterContext& stream_context) -> AiFilterSharedPtr {
    return DynamicModuleAiFilter::create(filter_config, stream_context);
  };
}

REGISTER_FACTORY(DynamicModuleAiFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
