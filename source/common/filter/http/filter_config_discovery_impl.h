#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/config/subscription.h"
#include "envoy/filter/http/filter_config_provider.h"
#include "envoy/http/filter.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/config/subscription_base.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Filter {
namespace Http {

class FilterConfigProviderManagerImpl;
class FilterConfigSubscription;

using FilterConfigSubscriptionSharedPtr = std::shared_ptr<FilterConfigSubscription>;

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImpl : public DynamicFilterConfigProvider {
public:
  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr& subscription,
                                  const absl::flat_hash_set<std::string>& require_type_urls,
                                  Server::Configuration::FactoryContext& factory_context,
                                  Envoy::Http::FilterFactoryCb default_config);
  ~DynamicFilterConfigProviderImpl() override;

  void validateTypeUrl(const std::string& type_url) const;

  // Config::ExtensionConfigProvider
  const std::string& name() override;
  absl::optional<Envoy::Http::FilterFactoryCb> config() override;

  // Config::DynamicExtensionConfigProvider
  void onConfigUpdate(Envoy::Http::FilterFactoryCb config, const std::string&,
                      Config::ConfigAppliedCb cb) override;
  void onConfigRemoved(Config::ConfigAppliedCb cb) override;
  void applyDefaultConfiguration() override {
    if (default_configuration_) {
      onConfigUpdate(*default_configuration_, "", nullptr);
    }
  }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig() : config_{absl::nullopt} {}
    absl::optional<Envoy::Http::FilterFactoryCb> config_{};
  };

  FilterConfigSubscriptionSharedPtr subscription_;
  const absl::flat_hash_set<std::string> require_type_urls_;
  // Currently applied configuration to ensure that the main thread deletes the last reference to
  // it.
  absl::optional<Envoy::Http::FilterFactoryCb> current_config_{absl::nullopt};
  const absl::optional<Envoy::Http::FilterFactoryCb> default_configuration_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;

  friend class FilterConfigProviderManagerImpl;
};

/**
 * All extension config discovery stats. @see stats_macros.h
 */
#define ALL_EXTENSION_CONFIG_DISCOVERY_STATS(COUNTER)                                              \
  COUNTER(config_reload)                                                                           \
  COUNTER(config_fail)                                                                             \
  COUNTER(config_conflict)

/**
 * Struct definition for all extension config discovery stats. @see stats_macros.h
 */
struct ExtensionConfigDiscoveryStats {
  ALL_EXTENSION_CONFIG_DISCOVERY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * A class that fetches the filter configuration dynamically using the filter config discovery API.
 * Subscriptions are shared between the filter config providers. The filter config providers are
 * notified when a new config is accepted.
 */
class FilterConfigSubscription
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>,
      Logger::Loggable<Logger::Id::filter> {
public:
  FilterConfigSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                           const std::string& filter_config_name,
                           Server::Configuration::FactoryContext& factory_context,
                           const std::string& stat_prefix,
                           FilterConfigProviderManagerImpl& filter_config_provider_manager,
                           const std::string& subscription_id);

  ~FilterConfigSubscription() override;

  const Init::SharedTargetImpl& initTarget() { return init_target_; }
  const std::string& name() { return filter_config_name_; }
  const absl::optional<Envoy::Http::FilterFactoryCb>& lastConfig() { return last_config_; }
  const std::string& lastTypeUrl() { return last_type_url_; }
  const std::string& lastVersionInfo() { return last_version_info_; }
  void incrementConflictCounter();

private:
  void start();

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override;
  void onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override;

  const std::string filter_config_name_;
  uint64_t last_config_hash_{0ul};
  absl::optional<Envoy::Http::FilterFactoryCb> last_config_{absl::nullopt};
  std::string last_type_url_;
  std::string last_version_info_;
  Server::Configuration::FactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;

  Init::SharedTargetImpl init_target_;
  bool started_{false};

  Stats::ScopePtr scope_;
  const std::string stat_prefix_;
  ExtensionConfigDiscoveryStats stats_;

  // FilterConfigProviderManagerImpl maintains active subscriptions in a map.
  FilterConfigProviderManagerImpl& filter_config_provider_manager_;
  const std::string subscription_id_;
  absl::flat_hash_set<DynamicFilterConfigProviderImpl*> filter_config_providers_;
  friend class DynamicFilterConfigProviderImpl;

  // This must be the last since its destructor may call out to stats to report
  // on draining requests.
  std::unique_ptr<Config::Subscription> subscription_;
};

/**
 * Provider implementation of a static filter config.
 **/
class StaticFilterConfigProviderImpl : public FilterConfigProvider {
public:
  StaticFilterConfigProviderImpl(const Envoy::Http::FilterFactoryCb& config,
                                 const std::string filter_config_name)
      : config_(config), filter_config_name_(filter_config_name) {}

  // Config::ExtensionConfigProvider
  const std::string& name() override { return filter_config_name_; }
  absl::optional<Envoy::Http::FilterFactoryCb> config() override { return config_; }

private:
  Envoy::Http::FilterFactoryCb config_;
  const std::string filter_config_name_;
};

/**
 * An implementation of FilterConfigProviderManager.
 */
class FilterConfigProviderManagerImpl : public FilterConfigProviderManager,
                                        public Singleton::Instance,
                                        Logger::Loggable<Logger::Id::filter> {
public:
  DynamicFilterConfigProviderPtr createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix) override;

  FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Envoy::Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl>(config, filter_config_name);
  }

private:
  std::shared_ptr<FilterConfigSubscription>
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name, Server::Configuration::FactoryContext& factory_context,
                  const std::string& stat_prefix);
  absl::flat_hash_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  friend class FilterConfigSubscription;
};

} // namespace Http
} // namespace Filter
} // namespace Envoy
