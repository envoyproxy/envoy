#include "source/extensions/access_loggers/stats/scope_provider_singleton.h"

#include "envoy/singleton/instance.h"
#include "envoy/type/v3/scope.pb.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Stats {

namespace {

struct ScopeConfiguration {
  Stats::ScopeStatsLimitSettings limits;
  bool evictable_;
};

ScopeConfiguration convertProtoToScopeStatsLimitSettings(const envoy::type::v3::Scope& config) {
  Stats::ScopeStatsLimitSettings limits;
  if (config.has_max_counters()) {
    limits.max_counters = config.max_counters().value();
  }
  if (config.has_max_gauges()) {
    limits.max_gauges = config.max_gauges().value();
  }
  if (config.has_max_histograms()) {
    limits.max_histograms = config.max_histograms().value();
  }
  return {limits, config.enable_eviction()};
}

} // namespace

SINGLETON_MANAGER_REGISTRATION(stats_access_logger_scope_provider);

Stats::ScopeSharedPtr
ScopeProviderSingleton::getScope(Server::Configuration::GenericFactoryContext& factory_context,
                                 const envoy::type::v3::Scope& config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (config.sharing_name().empty()) {
    ScopeConfiguration scope_cfg = convertProtoToScopeStatsLimitSettings(config);
    return factory_context.statsScope().createScope(config.prefix(), scope_cfg.evictable_,
                                                    scope_cfg.limits);
  }

  ScopeProviderSingletonSharedPtr provider =
      factory_context.serverFactoryContext().singletonManager().getTyped<ScopeProviderSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(stats_access_logger_scope_provider),
          [] { return std::make_shared<ScopeProviderSingleton>(); });

  size_t hash = MessageUtil::hash(config);
  auto it = provider->scopes_.find(hash);
  if (it != provider->scopes_.end()) {
    Stats::ScopeSharedPtr scope = it->second.lock();
    if (scope != nullptr) {
      return scope;
    }
  }

  ScopeConfiguration scope_cfg = convertProtoToScopeStatsLimitSettings(config);
  Stats::ScopeSharedPtr scope = factory_context.serverFactoryContext().serverScope().createScope(
      config.prefix(), scope_cfg.evictable_, scope_cfg.limits);

  Event::Dispatcher& dispatcher = factory_context.serverFactoryContext().mainThreadDispatcher();

  // The returned scope captures the provider (the shared_ptr) by value in its cleanup callback.
  // This keeps the provider (and singleton cache) alive as long as the scope exists.
  scope->setCleanupCallback([hash, provider, &dispatcher]() {
    dispatcher.post([hash, provider]() {
      auto map_it = provider->scopes_.find(hash);
      if (map_it != provider->scopes_.end() && map_it->second.expired()) {
        provider->scopes_.erase(map_it);
      }
    });
  });

  provider->scopes_[hash] = scope;

  return scope;
}

} // namespace Stats
} // namespace Envoy
