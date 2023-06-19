#pragma once

#include "envoy/server/tracer_config.h"
#include "envoy/singleton/instance.h"
#include "envoy/tracing/tracer_manager.h"

#include "source/common/common/logger.h"
#include "source/common/tracing/tracer_config_impl.h"
#include "source/common/tracing/tracer_impl.h"

namespace Envoy {
namespace Tracing {

/**
 * TracerManager implementation that manages the tracers. This should be used as a singleton except
 * in tests.
 */
class TracerManagerImpl : public TracerManager,
                          public Singleton::Instance,
                          Logger::Loggable<Logger::Id::tracing> {
public:
  TracerManagerImpl(Server::Configuration::TracerFactoryContextPtr factory_context);

  // TracerManager
  TracerSharedPtr getOrCreateTracer(const envoy::config::trace::v3::Tracing_Http* config) override;

  // Take a peek into the cache of HttpTracers. This should only be used in tests.
  const absl::flat_hash_map<std::size_t, std::weak_ptr<Tracer>>& peekCachedTracersForTest() const {
    return tracers_;
  }

  static std::shared_ptr<TracerManager> singleton(Server::Configuration::FactoryContext& context);

private:
  void removeExpiredCacheEntries();

  Server::Configuration::TracerFactoryContextPtr factory_context_;
  const TracerSharedPtr null_tracer_{std::make_shared<Tracing::NullTracer>()};

  // HttpTracers indexed by the hash of their configuration.
  absl::flat_hash_map<std::size_t, std::weak_ptr<Tracer>> tracers_;
};

} // namespace Tracing
} // namespace Envoy
