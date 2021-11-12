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
class DynamicFilterConfigProviderImplBase : public Config::DynamicExtensionConfigProviderBase {
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
template <class FactoryCb>
class DynamicFilterConfigProviderImpl : public DynamicFilterConfigProviderImplBase,
                                        public DynamicFilterConfigProvider<FactoryCb> {
public:
  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr& subscription,
                                  const absl::flat_hash_set<std::string>& require_type_urls,
                                  Server::Configuration::FactoryContext& factory_context,
                                  ProtobufTypes::MessagePtr&& default_config,
                                  bool last_filter_in_filter_chain,
                                  const std::string& filter_chain_type,
                                  std::function<FactoryCb(const Protobuf::Message&)> factory_cb_fn)
      : DynamicFilterConfigProviderImplBase(subscription, require_type_urls,
                                            last_filter_in_filter_chain, filter_chain_type),
        default_configuration_(std::move(default_config)), tls_(factory_context.threadLocal()),
        factory_cb_fn_(factory_cb_fn) {
    tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalConfig>(); });
  };

  // Config::ExtensionConfigProvider
  const std::string& name() override { return DynamicFilterConfigProviderImplBase::name(); }
  absl::optional<FactoryCb> config() override { return tls_->config_; }

  // Config::DynamicExtensionConfigProviderBase
  void onConfigUpdate(const Protobuf::Message& message, const std::string&,
                      Config::ConfigAppliedCb cb) override {
    const FactoryCb config = factory_cb_fn_(message);
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
    const absl::optional<FactoryCb> default_config =
        default_configuration_ ? absl::make_optional(factory_cb_fn_(*default_configuration_))
                               : absl::nullopt;
    tls_.runOnAllThreads(
        [config = default_config](OptRef<ThreadLocalConfig> tls) { tls->config_ = config; },
        [this, default_config, applied_on_all_threads]() {
          // This happens after all workers have discarded the previous config so it can be safely
          // deleted on the main thread by an update with the new config.
          this->current_config_ = default_config;
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
    absl::optional<FactoryCb> config_{};
  };

  // Currently applied configuration to ensure that the main thread deletes the last reference to
  // it.
  absl::optional<FactoryCb> current_config_{absl::nullopt};
  const ProtobufTypes::MessagePtr default_configuration_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
  const std::function<FactoryCb(const Protobuf::Message&)> factory_cb_fn_;
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
  const Protobuf::Message* lastConfig() { return last_config_.get(); }
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
  ProtobufTypes::MessagePtr last_config_;
  std::string last_type_url_;
  std::string last_version_info_;
  std::string last_filter_name_;
  bool last_filter_is_terminal_;
  Server::Configuration::FactoryContext& factory_context_;

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
template <class FactoryCb>
class StaticFilterConfigProviderImpl : public FilterConfigProvider<FactoryCb> {
public:
  StaticFilterConfigProviderImpl(const FactoryCb& config, const std::string filter_config_name)
      : config_(config), filter_config_name_(filter_config_name) {}

  // Config::ExtensionConfigProvider
  const std::string& name() override { return filter_config_name_; }
  absl::optional<FactoryCb> config() override { return config_; }

private:
  FactoryCb config_;
  const std::string filter_config_name_;
};

/**
 * Base class for a FilterConfigProviderManager.
 */
class FilterConfigProviderManagerImplBase : Logger::Loggable<Logger::Id::filter> {
public:
  virtual ~FilterConfigProviderManagerImplBase() = default;

  virtual std::tuple<ProtobufTypes::MessagePtr, std::string, bool>
  getMessage(const envoy::config::core::v3::TypedExtensionConfig& filter_config,
             Server::Configuration::FactoryContext& factory_context) const PURE;

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
template <class FactoryCb>
class FilterConfigProviderManagerImpl : public FilterConfigProviderManagerImplBase,
                                        public FilterConfigProviderManager<FactoryCb>,
                                        public Singleton::Instance {
public:
  DynamicFilterConfigProviderPtr<FactoryCb> createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
      const std::string& stat_prefix, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type) override {
    auto subscription = getSubscription(config_source.config_source(), filter_config_name,
                                        factory_context, stat_prefix);
    // For warming, wait until the subscription receives the first response to indicate readiness.
    // Otherwise, mark ready immediately and start the subscription on initialization. A default
    // config is expected in the latter case.
    if (!config_source.apply_default_config_without_warming()) {
      factory_context.initManager().add(subscription->initTarget());
    }
    absl::flat_hash_set<std::string> require_type_urls;
    for (const auto& type_url : config_source.type_urls()) {
      auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
      require_type_urls.emplace(factory_type_url);
    }

    ProtobufTypes::MessagePtr default_config;
    if (config_source.has_default_config()) {
      default_config =
          getDefaultConfig(config_source.default_config(), filter_config_name, factory_context,
                           last_filter_in_filter_chain, filter_chain_type, require_type_urls);
    }

    auto provider = std::make_unique<DynamicFilterConfigProviderImpl<FactoryCb>>(
        subscription, require_type_urls, factory_context, std::move(default_config),
        last_filter_in_filter_chain, filter_chain_type,
        [this, stat_prefix, &factory_context](const Protobuf::Message& message) -> FactoryCb {
          return instantiateFilterFactory(message, stat_prefix, factory_context);
        });

    // Ensure the subscription starts if it has not already.
    if (config_source.apply_default_config_without_warming()) {
      factory_context.initManager().add(provider->initTarget());
    }
    applyLastOrDefaultConfig(subscription, *provider, filter_config_name);
    return provider;
  }

  FilterConfigProviderPtr<FactoryCb>
  createStaticFilterConfigProvider(const FactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl<FactoryCb>>(config, filter_config_name);
  }

protected:
  virtual ProtobufTypes::MessagePtr
  getDefaultConfig(const ProtobufWkt::Any& proto_config, const std::string& filter_config_name,
                   Server::Configuration::FactoryContext& factory_context,
                   bool last_filter_in_filter_chain, const std::string& filter_chain_type,
                   const absl::flat_hash_set<std::string>& require_type_urls) const PURE;

  virtual FactoryCb
  instantiateFilterFactory(const Protobuf::Message& message, const std::string& stat_prefix,
                           Server::Configuration::FactoryContext& factory_context) const PURE;
};

class HttpFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<Http::FilterFactoryCb> {
public:
  std::tuple<ProtobufTypes::MessagePtr, std::string, bool>
  getMessage(const envoy::config::core::v3::TypedExtensionConfig& filter_config,
             Server::Configuration::FactoryContext& factory_context) const override;

protected:
  ProtobufTypes::MessagePtr
  getDefaultConfig(const ProtobufWkt::Any& proto_config, const std::string& filter_config_name,
                   Server::Configuration::FactoryContext& factory_context,
                   bool last_filter_in_filter_chain, const std::string& filter_chain_type,
                   const absl::flat_hash_set<std::string>& require_type_urls) const override;
  Http::FilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message, const std::string& stat_prefix,
                           Server::Configuration::FactoryContext& factory_context) const override;
};

} // namespace Filter
} // namespace Envoy
