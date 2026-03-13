#include "source/extensions/access_loggers/stats/scope_provider_singleton.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Stats {

namespace {

struct ScopeConfiguration {
  Stats::ScopeStatsLimitSettings limits;
  bool evictable;
};

ScopeConfiguration
convertProtoToScopeStatsLimitSettings(const envoy::type::v3::ScopeSettings& config) {
  Stats::ScopeStatsLimitSettings limits;
  limits.max_counters = config.max_counters();
  limits.max_gauges = config.max_gauges();
  limits.max_histograms = config.max_histograms();
  return {limits, config.evictable()};
}

} // namespace

SINGLETON_MANAGER_REGISTRATION(scope_provider);

Stats::ScopeSharedPtr
ScopeProviderSingleton::getScope(Server::Configuration::GenericFactoryContext& factory_context,
                                 const envoy::type::v3::Scope& config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto provider =
      factory_context.serverFactoryContext().singletonManager().getTyped<ScopeProviderSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(scope_provider),
          [] { return std::make_shared<ScopeProviderSingleton>(); });

  size_t hash = MessageUtil::hash(config);
  auto it = provider->scopes_.find(hash);
  if (it != provider->scopes_.end()) {
    Stats::ScopeSharedPtr scope = it->second.lock();
    if (scope != nullptr) {
      return scope;
    }
  }

  auto scope_config = convertProtoToScopeStatsLimitSettings(config.settings());

  Stats::ScopeSharedPtr scope = factory_context.scope().createScope(
      config.name(), scope_config.evictable, scope_config.limits);

  // We wrap the created scope in another shared_ptr with a custom deleter.
  // This custom deleter will automatically remove the corresponding entry
  // from the scopes_ map in the ScopeProviderSingleton whenever the last
  // reference to the scope is dropped. It also captures the provider instance
  // to keep the singleton and its map alive as long as any scopes are in use.
  Stats::ScopeSharedPtr wrapper(
      scope.get(), [scope, hash, provider](Stats::Scope*) { provider->scopes_.erase(hash); });

  provider->scopes_[hash] = wrapper;
  return wrapper;
}

} // namespace Stats
} // namespace Envoy
