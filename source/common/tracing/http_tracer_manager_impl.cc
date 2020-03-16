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
    auto http_tracer = it->second.lock();
    if (http_tracer) { // HttpTracer might have been released since it's a weak reference
      return http_tracer;
    }
  }

  // Initialize a new tracer.
  ENVOY_LOG(info, "instantiating a new tracer: {}", config->name());

  // Now see if there is a factory that will accept the config.
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<Server::Configuration::TracerFactory>(*config);
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      *config, factory_context_->messageValidationVisitor(), factory);

  HttpTracerSharedPtr http_tracer = factory.createHttpTracer(*message, *factory_context_);
  http_tracers_.insert_or_assign(cache_key, http_tracer); // cache a weak reference
  return http_tracer;
}

} // namespace Tracing
} // namespace Envoy
