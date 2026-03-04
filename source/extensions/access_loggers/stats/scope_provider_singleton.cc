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
    return it->second;
  }

  auto scope_config = convertProtoToScopeStatsLimitSettings(config.settings());

  Stats::ScopeSharedPtr scope = factory_context.scope().createScope(
      config.name(), scope_config.evictable, scope_config.limits);

  provider->scopes_.emplace(hash, scope);
  return scope;
}

} // namespace Stats
} // namespace Envoy
