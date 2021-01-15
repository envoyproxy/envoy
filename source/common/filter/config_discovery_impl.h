#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/config/subscription_base.h"
#include "common/config/utility.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Filter {

class FilterConfigSubscription;

using FilterConfigSubscriptionSharedPtr = std::shared_ptr<FilterConfigSubscription>;

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImplBase : public Config::ExtensionConfigProviderBase {
public:
  DynamicFilterConfigProviderImplBase(FilterConfigSubscriptionSharedPtr&& subscription,
                                      const std::set<std::string>& require_type_urls);
  virtual ~DynamicFilterConfigProviderImplBase();
  const Init::Target& initTarget() const { return init_target_; }

  // Config::ExtensionConfigProviderBase
  const std::string& name() override;
  void validateConfig(const ProtobufWkt::Any& proto_config) override;

private:
  FilterConfigSubscriptionSharedPtr subscription_;
  const std::set<std::string> require_type_urls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;
};

/**
 * Base class for a provider managing subscriptions.
 **/
class FilterConfigProviderManagerImplBase {
public:
  virtual ~FilterConfigProviderManagerImplBase() = default;

protected:
  FilterConfigSubscriptionSharedPtr
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name, Server::Configuration::FactoryContext& factory_context,
                  const std::string& stat_prefix);

private:
  absl::flat_hash_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  friend class FilterConfigSubscription;
};

/**
 * All extension config discovery stats. @see stats_macros.h
 */
#define ALL_EXTENSION_CONFIG_DISCOVERY_STATS(COUNTER)                                              \
  COUNTER(config_reload)                                                                           \
  COUNTER(config_fail)

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

template <class FactoryCb>
class DynamicFilterConfigProviderImpl : public DynamicFilterConfigProviderImplBase,
                                        public FilterConfigProvider<FactoryCb> {
public:
  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr&& subscription,
                                  const std::set<std::string>& require_type_urls,
                                  Server::Configuration::CommonFactoryContext& factory_context,
                                  std::function<FactoryCb(const ProtobufWkt::Any&)> factory_cb)
      : DynamicFilterConfigProviderImplBase(std::move(subscription), require_type_urls),
        tls_(factory_context.threadLocal()), factory_cb_(factory_cb) {
    tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalConfig>(); });
  }

  // Config::ExtensionConfigProvider
  const std::string& name() override { return DynamicFilterConfigProviderImplBase::name(); }
  void validateConfig(const ProtobufWkt::Any& proto_config) override {
    DynamicFilterConfigProviderImplBase::validateConfig(proto_config);
  }
  void onConfigUpdate(const ProtobufWkt::Any& proto_config, const std::string&,
                      Config::ConfigAppliedCb cb) override {
    // This should throw on the first provider update, which should prevent partial updates.
    // Ideally, this factory callback happens once during validation and propagated, but
    // it would require adding a template parameter to the base classes.
    FactoryCb config = factory_cb_(proto_config);
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
  absl::optional<FactoryCb> config() override { return tls_->config_; }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig() : config_{absl::nullopt} {}
    absl::optional<FactoryCb> config_{};
  };

  // Currently applied configuration to ensure that the main thread deletes the last reference to
  // it.
  absl::optional<FactoryCb> current_config_{absl::nullopt};
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
  std::function<FactoryCb(const ProtobufWkt::Any&)> factory_cb_;
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
  void validateConfig(const ProtobufWkt::Any&) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void onConfigUpdate(const ProtobufWkt::Any&, const std::string&,
                      Config::ConfigAppliedCb) override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

private:
  FactoryCb config_;
  const std::string filter_config_name_;
};

/**
 * An implementation of FilterConfigProviderManager.
 */
template <class FactoryCb>
class FilterConfigProviderManagerImpl : public FilterConfigProviderManagerImplBase,
                                        public FilterConfigProviderManager<FactoryCb>,
                                        public Singleton::Instance {
public:
  virtual ~FilterConfigProviderManagerImpl() = default;

  FilterConfigProviderPtr<FactoryCb> createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ConfigSource& config_source,
      const std::string& filter_config_name, const std::set<std::string>& require_type_urls,
      Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
      bool apply_without_warming) override {
    auto subscription =
        getSubscription(config_source, filter_config_name, factory_context, stat_prefix);
    // For warming, wait until the subscription receives the first response to indicate readiness.
    // Otherwise, mark ready immediately and start the subscription on initialization. A default
    // config is expected in the latter case.
    if (!apply_without_warming) {
      factory_context.initManager().add(subscription->initTarget());
    }
    auto provider = std::make_unique<DynamicFilterConfigProviderImpl<FactoryCb>>(
        std::move(subscription), require_type_urls, factory_context,
        [this, stat_prefix, &factory_context](const ProtobufWkt::Any& proto_config) -> FactoryCb {
          return instantiateFilterFactory(proto_config, stat_prefix, factory_context);
        });
    // Ensure the subscription starts if it has not already.
    if (apply_without_warming) {
      factory_context.initManager().add(provider->initTarget());
    }
    return provider;
  }

  FilterConfigProviderPtr<FactoryCb>
  createStaticFilterConfigProvider(const FactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl<FactoryCb>>(config, filter_config_name);
  }

protected:
  virtual FactoryCb
  instantiateFilterFactory(const ProtobufWkt::Any& proto_config, const std::string& stat_prefix,
                           Server::Configuration::FactoryContext& factory_context) = 0;
};

class HttpFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<Http::FilterFactoryCb> {
protected:
  Http::FilterFactoryCb
  instantiateFilterFactory(const ProtobufWkt::Any& proto_config, const std::string& stat_prefix,
                           Server::Configuration::FactoryContext& factory_context) override;
};

class NetworkFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<Network::FilterFactoryCb> {
protected:
  Network::FilterFactoryCb
  instantiateFilterFactory(const ProtobufWkt::Any& proto_config, const std::string& stat_prefix,
                           Server::Configuration::FactoryContext& factory_context) override;
};

} // namespace Filter
} // namespace Envoy
