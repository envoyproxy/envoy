#pragma once

#include <unordered_map>
#include <unordered_set>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/filter/filter_config_provider.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/config/subscription_base.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"

namespace Envoy {
namespace Filter {

class HttpFilterConfigProviderManagerImpl;
class HttpFilterConfigSubscription;

using HttpFilterConfigSubscriptionSharedPtr = std::shared_ptr<HttpFilterConfigSubscription>;

/**
 * Implementation of a filter config provider using discovery subscriptions.
 **/
class DynamicFilterConfigProviderImpl : public HttpFilterConfigProvider {
public:
  DynamicFilterConfigProviderImpl(HttpFilterConfigSubscriptionSharedPtr&& subscription,
                                  bool require_terminal,
                                  Server::Configuration::FactoryContext& factory_context);
  ~DynamicFilterConfigProviderImpl() override;

  // Config::ExtensionConfigProvider
  const std::string& name() override;
  absl::optional<Http::FilterFactoryCb> config() override;
  void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory&) override;
  void onConfigUpdate(Http::FilterFactoryCb config, const std::string&) override;

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig() : config_{absl::nullopt} {}
    absl::optional<Http::FilterFactoryCb> config_{};
  };

  HttpFilterConfigSubscriptionSharedPtr subscription_;
  const bool require_terminal_;
  ThreadLocal::SlotPtr tls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;

  friend class HttpFilterConfigProviderManagerImpl;
};

/**
 * All filter config discovery stats. @see stats_macros.h
 */
#define ALL_FILTER_CONFIG_DISCOVERY_STATS(COUNTER)                                                 \
  COUNTER(config_reload)                                                                           \
  COUNTER(config_fail)

/**
 * Struct definition for all filter config discovery stats. @see stats_macros.h
 */
struct FilterConfigDiscoveryStats {
  ALL_FILTER_CONFIG_DISCOVERY_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * A class that fetches the filter configuration dynamically using the filter config discovery API.
 * Subscriptions are shared between the filter config providers. The filter config providers are
 * notified when a new config is accepted.
 */
class HttpFilterConfigSubscription
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>,
      Logger::Loggable<Logger::Id::router> {
public:
  HttpFilterConfigSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                               const std::string& filter_config_name,
                               Server::Configuration::FactoryContext& factory_context,
                               const std::string& stat_prefix,
                               HttpFilterConfigProviderManagerImpl& filter_config_provider_manager,
                               const std::string& subscription_id);

  ~HttpFilterConfigSubscription() override;

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

  std::unique_ptr<Config::Subscription> subscription_;
  const std::string filter_config_name_;
  Server::Configuration::FactoryContext& factory_context_;
  ProtobufMessage::ValidationVisitor& validator_;

  Init::SharedTargetImpl init_target_;
  bool started_{false};

  Stats::ScopePtr scope_;
  const std::string stat_prefix_;
  FilterConfigDiscoveryStats stats_;

  // HttpFilterConfigProviderManagerImpl maintains active subscriptions in a map.
  HttpFilterConfigProviderManagerImpl& filter_config_provider_manager_;
  const std::string subscription_id_;
  std::unordered_set<DynamicFilterConfigProviderImpl*> filter_config_providers_;
  friend class DynamicFilterConfigProviderImpl;
};

/**
 * Provider implementation of a static filter config.
 **/
class StaticFilterConfigProviderImpl : public HttpFilterConfigProvider {
public:
  StaticFilterConfigProviderImpl(const Http::FilterFactoryCb& config,
                                 const std::string filter_config_name)
      : config_(config), filter_config_name_(filter_config_name) {}

  // Config::ExtensionConfigProvider
  const std::string& name() override { return filter_config_name_; }
  absl::optional<Http::FilterFactoryCb> config() override { return config_; }
  void validateConfig(Server::Configuration::NamedHttpFilterConfigFactory&) override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdate(Http::FilterFactoryCb, const std::string&) override {
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

private:
  Http::FilterFactoryCb config_;
  const std::string filter_config_name_;
};

/**
 * An implementation of HttpFilterConfigProviderManager.
 */
class HttpFilterConfigProviderManagerImpl : public HttpFilterConfigProviderManager,
                                            public Singleton::Instance {
public:
  ~HttpFilterConfigProviderManagerImpl() override{};

  HttpFilterConfigProviderManagerImpl() = default;

  HttpFilterConfigProviderPtr
  createDynamicFilterConfigProvider(const envoy::config::core::v3::ConfigSource& config_source,
                                    const std::string& filter_config_name, bool require_terminal,
                                    Server::Configuration::FactoryContext& factory_context,
                                    const std::string& stat_prefix,
                                    bool apply_without_warming) override;

  HttpFilterConfigProviderPtr
  createStaticFilterConfigProvider(const Http::FilterFactoryCb& config,
                                   const std::string& filter_config_name) override {
    return std::make_unique<StaticFilterConfigProviderImpl>(config, filter_config_name);
  }

private:
  std::shared_ptr<HttpFilterConfigSubscription>
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name, Server::Configuration::FactoryContext& factory_context,
                  const std::string& stat_prefix);
  std::unordered_map<std::string, std::weak_ptr<HttpFilterConfigSubscription>> subscriptions_;
  friend class HttpFilterConfigSubscription;
};

} // namespace Filter
} // namespace Envoy
