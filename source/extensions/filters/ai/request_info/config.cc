#include "source/extensions/filters/ai/request_info/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/ai/request_info/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
RequestInfoFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::ai::request_info::v3::RequestInfo&>(
      config, context.messageValidationVisitor());
  auto filter_config = std::make_shared<const RequestInfoFilterConfig>(proto, scope);
  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<RequestInfoFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(RequestInfoFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
