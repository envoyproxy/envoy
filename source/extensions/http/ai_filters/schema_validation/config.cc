#include "source/extensions/http/ai_filters/schema_validation/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/ai_filters/schema_validation/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace SchemaValidation {

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
SchemaValidationFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::ai_filters::schema_validation::v3::SchemaValidation&>(
      config, context.messageValidationVisitor());
  auto filter_config = std::make_shared<const SchemaValidationFilterConfig>(proto, scope);
  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<SchemaValidationFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(SchemaValidationFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace SchemaValidation
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
