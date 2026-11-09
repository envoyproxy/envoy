#include "source/extensions/http/ai_filters/model_router/config.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/registry/registry.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/http/ai_filters/model_router/filter.h"
#include "source/extensions/http/ai_filters/model_router/local_quota.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"
#include "source/extensions/http/ai_filters/model_router/redis_quota.h"
#include "source/extensions/http/ai_filters/model_router/rls_quota.h"

#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

using LocalQuotaConfigProto =
    envoy::extensions::http::ai_filters::model_router::v3::LocalQuotaConfig;
using ModelRouterProto = envoy::extensions::http::ai_filters::model_router::v3::ModelRouter;
using RedisQuotaConfigProto =
    envoy::extensions::http::ai_filters::model_router::v3::RedisQuotaConfig;
using RlsQuotaConfigProto = envoy::extensions::http::ai_filters::model_router::v3::RlsQuotaConfig;

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
  if (proto.has_quota_config()) {
    const auto& typed_config = proto.quota_config().typed_config();
    const std::string type = Config::Utility::getFactoryType(typed_config);
    ThreadLocal::SlotAllocator& slot_allocator = context.threadLocal();
    ModelRouterFilterConfig::QuotaPtr tl_quota =
        ThreadLocal::TypedSlot<QuotaInterface>::makeUnique(slot_allocator);
    if (type == LocalQuotaConfigProto::default_instance().GetTypeName()) {
      LocalQuotaConfigProto local_config;
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
          typed_config, context.messageValidationVisitor(), local_config));
      MessageUtil::validate(local_config, context.messageValidationVisitor());
      tl_quota->set([base_quota = LocalQuota(proto, context.timeSource())](
                        Event::Dispatcher&) -> std::shared_ptr<QuotaInterface> {
        return std::make_shared<LocalQuota>(base_quota);
      });
    } else if (type == RedisQuotaConfigProto::default_instance().GetTypeName()) {
      RedisQuotaConfigProto redis_config;
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
          typed_config, context.messageValidationVisitor(), redis_config));
      MessageUtil::validate(redis_config, context.messageValidationVisitor());
      tl_quota->set([proto, redis_config, &time_source = context.timeSource()](
                        Event::Dispatcher& dispatcher) -> std::shared_ptr<QuotaInterface> {
        return std::make_shared<RedisQuota>(proto, redis_config, dispatcher, time_source);
      });
    } else if (type == RlsQuotaConfigProto::default_instance().GetTypeName()) {
      RlsQuotaConfigProto rls_config;
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
          typed_config, context.messageValidationVisitor(), rls_config));
      MessageUtil::validate(rls_config, context.messageValidationVisitor());
      Grpc::AsyncClientFactoryPtr factory;
      ABSL_ASSIGN_OR_RETURN(factory,
                            context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
                                rls_config.grpc_service(), scope, true));
      std::shared_ptr<Grpc::AsyncClientFactory> shared_factory = std::move(factory);
      tl_quota->set([proto, rls_config,
                     shared_factory](Event::Dispatcher&) -> std::shared_ptr<QuotaInterface> {
        absl::StatusOr<Grpc::RawAsyncClientPtr> raw_client =
            shared_factory->createUncachedRawAsyncClient();
        Grpc::RawAsyncClientSharedPtr client = raw_client.ok() ? std::move(*raw_client) : nullptr;
        return std::make_shared<RlsQuota>(proto, rls_config, std::move(client));
      });
    } else {
      return absl::InvalidArgumentError(absl::StrCat("Unknown quota_config type: ", type));
    }
    filter_config->setQuotas(std::move(tl_quota));
  }

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
