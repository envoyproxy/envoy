#pragma once

#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache.pb.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

class DnsCacheManagerImpl : public DnsCacheManager, public Singleton::Instance {
public:
  DnsCacheManagerImpl(Event::Dispatcher& main_thread_dispatcher, ThreadLocal::SlotAllocator& tls,
                      Random::RandomGenerator& random, Filesystem::Instance& file_system,
                      Runtime::Loader& loader, Stats::Scope& root_scope,
                      ProtobufMessage::ValidationVisitor& validation_visitor)
      : main_thread_dispatcher_(main_thread_dispatcher), tls_(tls), random_(random),
        file_system_(file_system), loader_(loader), root_scope_(root_scope),
        validation_visitor_(validation_visitor) {}

  // DnsCacheManager
  DnsCacheSharedPtr getCache(
      const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config) override;

private:
  struct ActiveCache {
    ActiveCache(const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig& config,
                DnsCacheSharedPtr cache)
        : config_(config), cache_(cache) {}

    const envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig config_;
    DnsCacheSharedPtr cache_;
  };

  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_;
  Random::RandomGenerator& random_;
  Filesystem::Instance& file_system_;
  Runtime::Loader& loader_;
  Stats::Scope& root_scope_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  absl::flat_hash_map<std::string, ActiveCache> caches_;
};

class DnsCacheManagerFactoryImpl : public DnsCacheManagerFactory {
public:
  DnsCacheManagerFactoryImpl(Singleton::Manager& singleton_manager, Event::Dispatcher& dispatcher,
                             ThreadLocal::SlotAllocator& tls, Api::Api& api,
                             Runtime::Loader& loader, Stats::Scope& root_scope,
                             ProtobufMessage::ValidationVisitor& validation_visitor)
      : singleton_manager_(singleton_manager), dispatcher_(dispatcher), tls_(tls),
        random_(api.randomGenerator()), file_system_(api.fileSystem()), loader_(loader),
        root_scope_(root_scope), validation_visitor_(validation_visitor) {}

  DnsCacheManagerSharedPtr get() override;

private:
  Singleton::Manager& singleton_manager_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotAllocator& tls_;
  Random::RandomGenerator& random_;
  Filesystem::Instance& file_system_;
  Runtime::Loader& loader_;
  Stats::Scope& root_scope_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
