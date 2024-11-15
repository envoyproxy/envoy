#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/http_server_properties_cache.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

struct AlternateProtocolsData {
  AlternateProtocolsData(Server::Configuration::ServerFactoryContext& context,
                         ProtobufMessage::ValidationVisitor& validation_visitor)
      : dispatcher_(context.mainThreadDispatcher()), validation_visitor_(validation_visitor),
        file_system_(context.api().fileSystem()), concurrency_(context.options().concurrency()) {}

  Event::Dispatcher& dispatcher_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Filesystem::Instance& file_system_;
  uint32_t concurrency_;
};

class HttpServerPropertiesCacheManagerImpl : public HttpServerPropertiesCacheManager,
                                             public Singleton::Instance {
public:
  HttpServerPropertiesCacheManagerImpl(Server::Configuration::ServerFactoryContext& context,
                                       ProtobufMessage::ValidationVisitor& validation_visitor,
                                       ThreadLocal::SlotAllocator& tls);

  // HttpServerPropertiesCacheManager
  HttpServerPropertiesCacheSharedPtr
  getCache(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
           Event::Dispatcher& dispatcher) override;

  void forEachThreadLocalCache(CacheFn cache_fn) override;

private:
  // Contains a cache and the options associated with it.
  struct CacheWithOptions {
    CacheWithOptions(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
                     HttpServerPropertiesCacheSharedPtr cache)
        : options_(options), cache_(cache) {}

    const envoy::config::core::v3::AlternateProtocolsCacheOptions options_;
    HttpServerPropertiesCacheSharedPtr cache_;
  };

  // Per-thread state.
  struct State : public ThreadLocal::ThreadLocalObject {
    // Map from config name to cache for that config.
    absl::flat_hash_map<std::string, CacheWithOptions> caches_;
  };

  AlternateProtocolsData data_;

  // Thread local state for the cache.
  ThreadLocal::TypedSlot<State> slot_;
};

} // namespace Http
} // namespace Envoy
