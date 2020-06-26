#pragma once

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

/**
 * A class that fetches the filter configuration dynamically using the filter config discovery API.
 */
class FilterConfigSubscription
    : Envoy::Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>,
      Logger::Loggable<Logger::Id::router> {
public:
  ~FilterConfigSubscription() override;

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(
      ABSL_ATTRIBUTE_UNUSED const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
      ABSL_ATTRIBUTE_UNUSED const std::string& version_info) override {}
  void onConfigUpdate(
      ABSL_ATTRIBUTE_UNUSED const
          Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
      ABSL_ATTRIBUTE_UNUSED const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      ABSL_ATTRIBUTE_UNUSED const std::string&) override {}
  void onConfigUpdateFailed(ABSL_ATTRIBUTE_UNUSED Envoy::Config::ConfigUpdateFailureReason reason,
                            ABSL_ATTRIBUTE_UNUSED const EnvoyException* e) override {}
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::config::core::v3::TypedExtensionConfig>(resource).name();
  }

  FilterConfigSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                           const std::string& name,
                           Server::Configuration::ServerFactoryContext& factory_context,
                           const std::string& stat_prefix);

  std::unique_ptr<Envoy::Config::Subscription> subscription_;
  const std::string filter_config_name_;
  ABSL_ATTRIBUTE_UNUSED Server::Configuration::ServerFactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;

  // Parent init target initializes local init manager, which in turn
  // initializes local init target to start a subscription. The subscription
  // signals the watcher to notify the parent init target readiness.
  Init::SharedTargetImpl parent_init_target_;
  Init::WatcherImpl local_init_watcher_;
  Init::TargetImpl local_init_target_;
  Init::ManagerImpl local_init_manager_;

  Stats::ScopePtr scope_;
};

using FilterConfigSubscriptionSharedPtr = std::shared_ptr<FilterConfigSubscription>;

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImpl : public FilterConfigProvider {
public:
  // Router::FilterConfigProvider
  absl::optional<Http::FilterFactoryCb> config() override { return {}; }
  void validateConfig(
      ABSL_ATTRIBUTE_UNUSED Server::Configuration::NamedHttpFilterConfigFactory& factory) override {
  }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    // ThreadLocalConfig(ConfigConstSharedPtr initial_config) : config_(std::move(initial_config))
    // {} ConfigConstSharedPtr config_;
  };

  DynamicFilterConfigProviderImpl(FilterConfigSubscriptionSharedPtr&& subscription,
                                  Server::Configuration::ServerFactoryContext& factory_context);

  FilterConfigSubscriptionSharedPtr subscription_;
  ABSL_ATTRIBUTE_UNUSED Server::Configuration::ServerFactoryContext& factory_context_;
  ThreadLocal::SlotPtr tls_;
};

/**
 * Provider implementation of a static filter config.
 **/
class StaticFilterConfigProviderImpl : public FilterConfigProvider {
public:
  StaticFilterConfigProviderImpl(const Http::FilterFactoryCb& callback) : callback_(callback) {}

  // Router::FilterConfigProvider
  absl::optional<Http::FilterFactoryCb> config() override { return callback_; }
  void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory&) override {}

private:
  Http::FilterFactoryCb callback_;
};

/**
 * Implementation of a filter config provider manager for both static and dynamic providers.
 **/
class FilterConfigProviderManagerImpl : public FilterConfigProviderManager,
                                        public Singleton::Instance {
public:
  ~FilterConfigProviderManagerImpl() override{};

  FilterConfigProviderManagerImpl() = default;

  FilterConfigProviderPtr createDynamicFilterConfigProvider(
      ABSL_ATTRIBUTE_UNUSED const envoy::config::core::v3::ConfigSource& config_source,
      ABSL_ATTRIBUTE_UNUSED const std::string& name,
      ABSL_ATTRIBUTE_UNUSED Server::Configuration::ServerFactoryContext& factory_context,
      ABSL_ATTRIBUTE_UNUSED const std::string& stat_prefix,
      ABSL_ATTRIBUTE_UNUSED Init::Manager& init_manager) override {
    return nullptr;
  }

  FilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& callback) override {
    return std::make_unique<StaticFilterConfigProviderImpl>(callback);
  }
};

} // namespace Router
} // namespace Envoy
