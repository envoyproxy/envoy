#include "common/tracing/http_tracer_manager_impl.h"

#include "common/config/utility.h"

namespace Envoy {
namespace Tracing {

HttpTracerManagerImpl::HttpTracerManagerImpl(
    Server::Configuration::TracerFactoryContextPtr factory_context)
    : factory_context_(std::move(factory_context)) {}

HttpTracerSharedPtr HttpTracerManagerImpl::getOrCreateHttpTracer(
    const envoy::config::trace::v3::Tracing& configuration) {
  if (!configuration.has_http()) {
    return null_tracer_;
  }

  const auto cache_key = MessageUtil::hash(configuration);
  const auto it = http_tracers_.find(cache_key);
  if (it != http_tracers_.end()) {
    return it->second;
  }

  // Initialize tracing driver.
  ENVOY_LOG(info, "instantiating tracing driver: {}", configuration.http().name());

  // Now see if there is a factory that will accept the config.
  auto& factory = Envoy::Config::Utility::getAndCheckFactory<Server::Configuration::TracerFactory>(
      configuration.http());
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      configuration.http(), factory_context_->messageValidationVisitor(), factory);

  HttpTracerSharedPtr http_tracer = factory.createHttpTracer(*message, *factory_context_);
  http_tracers_.emplace(cache_key, http_tracer);
  return http_tracer;
}

} // namespace Tracing
} // namespace Envoy
