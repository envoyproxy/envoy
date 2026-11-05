#include "source/extensions/http/ai_filters/model_router/config.h"

#include <memory>
#include <utility>

#include "envoy/registry/registry.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"
#include "source/extensions/http/ai_filters/model_router/local_quota.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

#include "absl/status/status_macros.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
ModelRouterFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope& scope) {
  const auto& proto = MessageUtil::downcastAndValidate<const ModelRouterProto&>(
      config, context.messageValidationVisitor());

  std::shared_ptr<ModelRouterFilterConfig> filter_config;
  ABSL_ASSIGN_OR_RETURN(filter_config,
                        ModelRouterFilterConfig::create(proto, context.api(), scope));

  // Create quota manager after config is validated by ModelRouterFilterConfig
  ThreadLocal::SlotAllocator& slot_allocator = context.threadLocal();
  ModelRouterFilterConfig::QuotaPtr tl_quota =
      ThreadLocal::TypedSlot<QuotaInterface>::makeUnique(slot_allocator);
  tl_quota->set([base_quota = LocalQuota(proto, context.timeSource())](Event::Dispatcher&) {
    return std::make_shared<LocalQuota>(base_quota);
  });
  filter_config->setQuotas(std::move(tl_quota));

  return [filter_config](const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<ModelRouterFilter>(filter_config, stream_context);
  };
}

REGISTER_FACTORY(ModelRouterFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
