#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/alternate_protocols_cache.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

struct AlternateProtocolsData {
  AlternateProtocolsData(Server::Configuration::FactoryContextBase& context)
      : dispatcher_(context.mainThreadDispatcher()),
        validation_visitor_(context.messageValidationVisitor()),
        file_system_(context.api().fileSystem()), concurrency_(context.options().concurrency()) {}
  Event::Dispatcher& dispatcher_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Filesystem::Instance& file_system_;
  uint32_t concurrency_;
};

class AlternateProtocolsCacheManagerImpl : public AlternateProtocolsCacheManager,
                                           public Singleton::Instance {
public:
  AlternateProtocolsCacheManagerImpl(AlternateProtocolsData& data, ThreadLocal::SlotAllocator& tls);

  // AlternateProtocolsCacheManager
  AlternateProtocolsCacheSharedPtr
  getCache(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
           Event::Dispatcher& dispatcher) override;

private:
  // Contains a cache and the options associated with it.
  struct CacheWithOptions {
    CacheWithOptions(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
                     AlternateProtocolsCacheSharedPtr cache)
        : options_(options), cache_(cache) {}

    const envoy::config::core::v3::AlternateProtocolsCacheOptions options_;
    AlternateProtocolsCacheSharedPtr cache_;
  };

  // Per-thread state.
  struct State : public ThreadLocal::ThreadLocalObject {
    // Map from config name to cache for that config.
    absl::flat_hash_map<std::string, CacheWithOptions> caches_;
  };

  AlternateProtocolsData& data_;

  // Thread local state for the cache.
  ThreadLocal::TypedSlot<State> slot_;
};

class AlternateProtocolsCacheManagerFactoryImpl : public AlternateProtocolsCacheManagerFactory {
public:
  AlternateProtocolsCacheManagerFactoryImpl(Singleton::Manager& singleton_manager,
                                            ThreadLocal::SlotAllocator& tls,
                                            AlternateProtocolsData data)
      : singleton_manager_(singleton_manager), tls_(tls), data_(data) {}

  AlternateProtocolsCacheManagerSharedPtr get() override;

private:
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;
  AlternateProtocolsData data_;
};

} // namespace Http
} // namespace Envoy
