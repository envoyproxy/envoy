#include "common/tracing/http_tracer_manager_impl.h"

#include "common/config/utility.h"

namespace Envoy {
namespace Tracing {

HttpTracerManagerImpl::HttpTracerManagerImpl(
    Server::Configuration::TracerFactoryContextPtr factory_context)
    : factory_context_(std::move(factory_context)) {}

HttpTracerSharedPtr
HttpTracerManagerImpl::getOrCreateHttpTracer(const envoy::config::trace::v3::Tracing_Http* config) {
  if (!config) {
    return null_tracer_;
  }

  const auto cache_key = MessageUtil::hash(*config);
  const auto it = http_tracers_.find(cache_key);
  if (it != http_tracers_.end()) {
    return it->second;
  }

  // Initialize tracing driver.
  ENVOY_LOG(info, "instantiating tracing driver: {}", config->name());

  // Now see if there is a factory that will accept the config.
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<Server::Configuration::TracerFactory>(*config);
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      *config, factory_context_->messageValidationVisitor(), factory);

  HttpTracerSharedPtr http_tracer = factory.createHttpTracer(*message, *factory_context_);
  // TODO(yskopets): In the initial implementation HttpTracers are never removed from the cache.
  // Once HttpTracer implementations have been revised to support removal and cleanup,
  // this cache must be reworked to release HttpTracers as soon as they are no longer in use.
  http_tracers_.emplace(cache_key, http_tracer);
  return http_tracer;
}

} // namespace Tracing
} // namespace Envoy
