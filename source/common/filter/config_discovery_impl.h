#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/config/subscription.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/http/filter.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/assert.h"
#include "source/common/config/subscription_base.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Filter {

class FilterConfigProviderManagerImplBase;
class FilterConfigSubscription;

using FilterConfigSubscriptionSharedPtr = std::shared_ptr<FilterConfigSubscription>;

/**
 * Base class for a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImplBase
    : public Config::DynamicExtensionConfigProviderBase<Envoy::Http::FilterFactoryCb> {
public:
  DynamicFilterConfigProviderImplBase(FilterConfigSubscriptionSharedPtr& subscription,
                                      const absl::flat_hash_set<std::string>& require_type_urls,
                                      bool last_filter_in_filter_chain,
                                      const std::string& filter_chain_type);

  ~DynamicFilterConfigProviderImplBase() override;
  const Init::Target& initTarget() const { return init_target_; }

  void validateTypeUrl(const std::string& type_url) const;
  void validateTerminalFilter(const std::string& name, const std::string& filter_type,
                              bool is_terminal_filter);

  const std::string& name();

private:
  FilterConfigSubscriptionSharedPtr subscription_;
  const absl::flat_hash_set<std::string> require_type_urls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;

  const bool last_filter_in_filter_chain_;
  const std::string filter_chain_type_;
};

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImpl : public DynamicFilterConfigProviderImplBase,
                                        public DynamicFilterConfigProvider {
public:
  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr& subscription,
                                  const absl::flat_hash_set<std::string>& require_type_urls,
                                  Server::Configuration::FactoryContext& factory_context,
                                  Envoy::Http::FilterFactoryCb default_config,
                                  bool last_filter_in_filter_chain,
                                  const std::string& filter_chain_type)
      : DynamicFilterConfigProviderImplBase(subscription, require_type_urls,
                                            last_filter_in_filter_chain, filter_chain_type),
        default_configuration_(default_config ? absl::make_optional(default_config)
                                              : absl::nullopt),
        tls_(factory_context.threadLocal()) {
    tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalConfig>(); });
  };

  // Config::ExtensionConfigProvider
  const std::string& name() override { return DynamicFilterConfigProviderImplBase::name(); }
  absl::optional<Envoy::Http::FilterFactoryCb> config() override { return tls_->config_; }

  // Config::DynamicExtensionConfigProvider
  void onConfigUpdate(Envoy::Http::FilterFactoryCb config, const std::string&,
                      Config::ConfigAppliedCb cb) override {
    tls_.runOnAllThreads(
        [config, cb](OptRef<ThreadLocalConfig> tls) {
          tls->config_ = config;
          if (cb) {
            cb();
          }
        },
        [this, config]() {
          // This happens after all workers have discarded the previous config so it can be safely
          // deleted on the main thread by an update with the new config.
          this->current_config_ = config;
        });
  }

  void onConfigRemoved(Config::ConfigAppliedCb applied_on_all_threads) override {
    tls_.runOnAllThreads(
        [config = default_configuration_](OptRef<ThreadLocalConfig> tls) { tls->config_ = config; },
        [this, applied_on_all_threads]() {
          // This happens after all workers have discarded the previous config so it can be safely
          // deleted on the main thread by an update with the new config.
          this->current_config_ = default_configuration_;
          if (applied_on_all_threads) {
            applied_on_all_threads();
          }
        });
  }

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

  // Currently applied configuration to ensure that the main thread deletes the last reference to
  // it.
  absl::optional<Envoy::Http::FilterFactoryCb> current_config_{absl::nullopt};
  const absl::optional<Envoy::Http::FilterFactoryCb> default_configuration_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
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
                           FilterConfigProviderManagerImplBase& filter_config_provider_manager,
                           const std::string& subscription_id);

  ~FilterConfigSubscription() override;

  const Init::SharedTargetImpl& initTarget() { return init_target_; }
  const std::string& name() { return filter_config_name_; }
  const absl::optional<Envoy::Http::FilterFactoryCb>& lastConfig() { return last_config_; }
  const std::string& lastTypeUrl() { return last_type_url_; }
  const std::string& lastVersionInfo() { return last_version_info_; }
  const std::string& lastFilterName() { return last_filter_name_; }
  bool isLastFilterTerminal() { return last_filter_is_terminal_; }
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
  std::string last_filter_name_;
  bool last_filter_is_terminal_;
  Server::Configuration::FactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;

  Init::SharedTargetImpl init_target_;
  bool started_{false};

  Stats::ScopePtr scope_;
  const std::string stat_prefix_;
  ExtensionConfigDiscoveryStats stats_;

  // FilterConfigProviderManagerImplBase maintains active subscriptions in a map.
  FilterConfigProviderManagerImplBase& filter_config_provider_manager_;
  const std::string subscription_id_;
  absl::flat_hash_set<DynamicFilterConfigProviderImplBase*> filter_config_providers_;
  friend class DynamicFilterConfigProviderImplBase;

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
 * Base class for a FilterConfigProviderManager.
 */
class FilterConfigProviderManagerImplBase : Logger::Loggable<Logger::Id::filter> {
protected:
  std::shared_ptr<FilterConfigSubscription>
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name, Server::Configuration::FactoryContext& factory_context,
                  const std::string& stat_prefix);
  void applyLastOrDefaultConfig(std::shared_ptr<FilterConfigSubscription>& subscription,
                                DynamicFilterConfigProviderImplBase& provider,
                                const std::string& filter_config_name);

private:
  absl::flat_hash_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  friend class FilterConfigSubscription;
};

/**
 * An implementation of FilterConfigProviderManager.
 */
class FilterConfigProviderManagerImpl : public FilterConfigProviderManagerImplBase,
                                        public FilterConfigProviderManager,
                                        public Singleton::Instance {
public:
  DynamicFilterConfigProviderPtr createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type) override;

  FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Envoy::Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl>(config, filter_config_name);
  }

protected:
  virtual Http::FilterFactoryCb
  getDefaultConfig(const ProtobufWkt::Any& proto_config, const std::string& filter_config_name,
                   Server::Configuration::FactoryContext& factory_context,
                   const std::string& stat_prefix, bool last_filter_in_filter_chain,
                   const std::string& filter_chain_type,
                   const absl::flat_hash_set<std::string> require_type_urls) const PURE;
};

class HttpFilterConfigProviderManagerImpl : public FilterConfigProviderManagerImpl {
protected:
  Http::FilterFactoryCb
  getDefaultConfig(const ProtobufWkt::Any& proto_config, const std::string& filter_config_name,
                   Server::Configuration::FactoryContext& factory_context,
                   const std::string& stat_prefix, bool last_filter_in_filter_chain,
                   const std::string& filter_chain_type,
                   const absl::flat_hash_set<std::string> require_type_urls) const override;
};

} // namespace Filter
} // namespace Envoy
