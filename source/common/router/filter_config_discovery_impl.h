#pragma once

#include <unordered_map>
#include <unordered_set>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/router/filter_config_provider.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"

#include "common/config/subscription_base.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"

namespace Envoy {
namespace Router {

class FilterConfigProviderManagerImpl;
class FilterConfigSubscription;

using FilterConfigSubscriptionSharedPtr = std::shared_ptr<FilterConfigSubscription>;

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImpl : public FilterConfigProvider {
public:
  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr&& subscription,
                                  bool require_terminal,
                                  Server::Configuration::FactoryContext& factory_context);
  ~DynamicFilterConfigProviderImpl() override;

  // Router::FilterConfigProvider
  const std::string& name() override;
  absl::optional<Http::FilterFactoryCb> config() override;
  void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory& factory) override;
  void onConfigUpdate(Http::FilterFactoryCb config, const std::string&) override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig() : config_{absl::nullopt} {}
    absl::optional<Http::FilterFactoryCb> config_{};
  };

  FilterConfigSubscriptionSharedPtr subscription_;
  const bool require_terminal_;
  ThreadLocal::SlotPtr tls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;

  friend class FilterConfigProviderManagerImpl;
};

/**
 * A class that fetches the filter configuration dynamically using the filter config discovery API.
 * Subscriptions are shared between the filter config providers. The filter config providers are
 * notified when a new config is accepted.
 */
class FilterConfigSubscription
    : Envoy::Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>,
      Logger::Loggable<Logger::Id::router> {
public:
  FilterConfigSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                           const std::string& filter_config_name,
                           Server::Configuration::FactoryContext& factory_context,
                           const std::string& stat_prefix,
                           FilterConfigProviderManagerImpl& filter_config_provider_manager,
                           const std::string& subscription_id);

  ~FilterConfigSubscription() override;

  const Init::SharedTargetImpl& initTarget() { return parent_init_target_; }
  const std::string& name() { return filter_config_name_; }

private:
  void start();

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(
      const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::core::v3::TypedExtensionConfig>(resource).name();
  }

  std::unique_ptr<Envoy::Config::Subscription> subscription_;
  const std::string filter_config_name_;
  Server::Configuration::FactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;

  // Parent init target initializes local init manager, which in turn
  // initializes local init target to start a subscription. The subscription
  // signals the watcher to notify the parent init target readiness.
  Init::SharedTargetImpl parent_init_target_;
  Init::WatcherImpl local_init_watcher_;
  Init::TargetImpl local_init_target_;
  Init::ManagerImpl local_init_manager_;
  bool started_{false};

  Stats::ScopePtr scope_;
  const std::string stat_prefix_;

  // FilterConfigProviderManager maintains active subscriptions in a map.
  FilterConfigProviderManagerImpl& filter_config_provider_manager_;
  const std::string subscription_id_;
  std::unordered_set<DynamicFilterConfigProviderImpl*> filter_config_providers_;
  friend class DynamicFilterConfigProviderImpl;
};

/**
 * Provider implementation of a static filter config.
 **/
class StaticFilterConfigProviderImpl : public FilterConfigProvider {
public:
  StaticFilterConfigProviderImpl(const Http::FilterFactoryCb& config,
                                 const std::string filter_config_name)
      : config_(config), filter_config_name_(filter_config_name) {}

  // Router::FilterConfigProvider
  const std::string& name() override { return filter_config_name_; }
  absl::optional<Http::FilterFactoryCb> config() override { return config_; }
  void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory&) override {}
  void onConfigUpdate(Http::FilterFactoryCb, const std::string&) override {}

private:
  Http::FilterFactoryCb config_;
  const std::string filter_config_name_;
};

/**
 * Implementation of a filter config provider manager for both static and dynamic providers.
 **/
class FilterConfigProviderManagerImpl : public FilterConfigProviderManager,
                                        public Singleton::Instance {
public:
  ~FilterConfigProviderManagerImpl() override{};

  FilterConfigProviderManagerImpl() = default;

  FilterConfigProviderPtr
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ConfigSource& config_source,
                                    const std::string& filter_config_name, bool require_terminal,
                                    Server::Configuration::FactoryContext& factory_context,
                                    const std::string& stat_prefix,
                                    bool apply_without_warming) override;

  FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl>(config, filter_config_name);
  }

private:
  std::shared_ptr<FilterConfigSubscription>
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name, Server::Configuration::FactoryContext& factory_context,
                  const std::string& stat_prefix);
  std::unordered_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  friend class FilterConfigSubscription;
};

} // namespace Router
} // namespace Envoy
