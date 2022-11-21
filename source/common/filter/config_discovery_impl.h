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
#include "source/common/config/utility.h"
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
  virtual void validateMessage(const std::string& config_name, const Protobuf::Message& message,
                               const std::string& factory_name) const PURE;

  const std::string& name();

private:
  FilterConfigSubscriptionSharedPtr subscription_;
  const absl::flat_hash_set<std::string> require_type_urls_;

  // Local initialization target to ensure that the subscription starts in
  // case no warming is requested by any other filter config provider.
  Init::TargetImpl init_target_;

protected:
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
  DynamicFilterConfigProviderImpl(
      FilterConfigSubscriptionSharedPtr& subscription,
      const absl::flat_hash_set<std::string>& require_type_urls, ThreadLocal::SlotAllocator& tls,
      ProtobufTypes::MessagePtr&& default_config, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type, absl::string_view stat_prefix,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher)
      : DynamicFilterConfigProviderImplBase(subscription, require_type_urls,
                                            last_filter_in_filter_chain, filter_chain_type),
        listener_filter_matcher_(listener_filter_matcher), stat_prefix_(stat_prefix),
        main_config_(std::make_shared<MainConfig>()),
        default_configuration_(std::move(default_config)), tls_(tls) {
    tls_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalConfig>(); });
  };

  ~DynamicFilterConfigProviderImpl() override {
    // Issuing an empty update to guarantee that the shared current config is
    // deleted on the main thread last. This is required if the current config
    // holds its own TLS.
    if (!tls_.isShutdown()) {
      update(absl::nullopt, nullptr);
    }
  }

  // Config::ExtensionConfigProvider
  const std::string& name() override { return DynamicFilterConfigProviderImplBase::name(); }
  OptRef<FactoryCb> config() override {
    if (auto& optional_config = tls_->config_; optional_config.has_value()) {
      return optional_config.value();
    }
    return {};
  }

  // Config::DynamicExtensionConfigProviderBase
  void onConfigUpdate(const Protobuf::Message& message, const std::string&,
                      Config::ConfigAppliedCb applied_on_all_threads) override {
    const FactoryCb config = instantiateFilterFactory(message);
    update(config, applied_on_all_threads);
  }

  void onConfigRemoved(Config::ConfigAppliedCb applied_on_all_threads) override {
    const absl::optional<FactoryCb> default_config =
        default_configuration_
            ? absl::make_optional(instantiateFilterFactory(*default_configuration_))
            : absl::nullopt;
    update(default_config, applied_on_all_threads);
  }

  void applyDefaultConfiguration() override {
    if (default_configuration_) {
      onConfigUpdate(*default_configuration_, "", nullptr);
    }
  }
  const Network::ListenerFilterMatcherSharedPtr& getListenerFilterMatcher() override {
    return listener_filter_matcher_;
  }

protected:
  const std::string& getStatPrefix() const { return stat_prefix_; }
  const Network::ListenerFilterMatcherSharedPtr listener_filter_matcher_;

private:
  virtual FactoryCb instantiateFilterFactory(const Protobuf::Message& message) const PURE;

  void update(absl::optional<FactoryCb> config, Config::ConfigAppliedCb applied_on_all_threads) {
    // This call must not capture 'this' as it is invoked on all workers asynchronously.
    tls_.runOnAllThreads([config](OptRef<ThreadLocalConfig> tls) { tls->config_ = config; },
                         [main_config = main_config_, config, applied_on_all_threads]() {
                           // This happens after all workers have discarded the previous config so
                           // it can be safely deleted on the main thread by an update with the new
                           // config.
                           main_config->current_config_ = config;
                           if (applied_on_all_threads) {
                             applied_on_all_threads();
                           }
                         });
  }

  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig() : config_{absl::nullopt} {}
    absl::optional<FactoryCb> config_{};
  };

  // Currently applied configuration to ensure that the main thread deletes the last reference to
  // it. Filter factories may hold their own thread local storage which is required to be deleted
  // on the main thread.
  struct MainConfig {
    absl::optional<FactoryCb> current_config_{absl::nullopt};
  };

  const std::string stat_prefix_;
  std::shared_ptr<MainConfig> main_config_;
  const ProtobufTypes::MessagePtr default_configuration_;
  ThreadLocal::TypedSlot<ThreadLocalConfig> tls_;
};

// Struct of canonical filter name and HTTP stream filter factory callback.
struct NamedHttpFilterFactoryCb {
  // Canonical filter name.
  std::string name;
  // Factory function used to create filter instances.
  Http::FilterFactoryCb factory_cb;
};

// Implementation of a HTTP dynamic filter config provider.
// NeutralHttpFilterConfigFactory can either be a NamedHttpFilterConfigFactory
// or an UpstreamHttpFilterConfigFactory.
template <class FactoryCtx, class NeutralHttpFilterConfigFactory>
class HttpDynamicFilterConfigProviderImpl
    : public DynamicFilterConfigProviderImpl<NamedHttpFilterFactoryCb> {
public:
  HttpDynamicFilterConfigProviderImpl(
      FilterConfigSubscriptionSharedPtr& subscription,
      const absl::flat_hash_set<std::string>& require_type_urls,
      Server::Configuration::ServerFactoryContext& server_context, FactoryCtx& factory_context,
      ProtobufTypes::MessagePtr&& default_config, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type, absl::string_view stat_prefix,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher)
      : DynamicFilterConfigProviderImpl(subscription, require_type_urls,
                                        server_context.threadLocal(), std::move(default_config),
                                        last_filter_in_filter_chain, filter_chain_type, stat_prefix,
                                        listener_filter_matcher),
        server_context_(server_context), factory_context_(factory_context) {}
  void validateMessage(const std::string& config_name, const Protobuf::Message& message,
                       const std::string& factory_name) const override {
    auto* factory =
        Registry::FactoryRegistry<NeutralHttpFilterConfigFactory>::getFactory(factory_name);
    const bool is_terminal_filter = factory->isTerminalFilterByProto(message, server_context_);
    Config::Utility::validateTerminalFilters(config_name, factory_name, filter_chain_type_,
                                             is_terminal_filter, last_filter_in_filter_chain_);
  }

private:
  NamedHttpFilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message) const override {
    auto* factory = Registry::FactoryRegistry<NeutralHttpFilterConfigFactory>::getFactoryByType(
        message.GetTypeName());
    return {factory->name(),
            factory->createFilterFactoryFromProto(message, getStatPrefix(), factory_context_)};
  }

  Server::Configuration::ServerFactoryContext& server_context_;
  FactoryCtx& factory_context_;
};

// Implementation of a listener dynamic filter config provider.
template <class FactoryCb>
class ListenerDynamicFilterConfigProviderImpl : public DynamicFilterConfigProviderImpl<FactoryCb> {
public:
  ListenerDynamicFilterConfigProviderImpl(
      FilterConfigSubscriptionSharedPtr& subscription,
      const absl::flat_hash_set<std::string>& require_type_urls,
      Server::Configuration::ServerFactoryContext&,
      Server::Configuration::ListenerFactoryContext& factory_context,
      ProtobufTypes::MessagePtr&& default_config, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type, absl::string_view stat_prefix,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher)
      : DynamicFilterConfigProviderImpl<FactoryCb>(
            subscription, require_type_urls, factory_context.threadLocal(),
            std::move(default_config), last_filter_in_filter_chain, filter_chain_type, stat_prefix,
            listener_filter_matcher),
        factory_context_(factory_context) {}

  void validateMessage(const std::string&, const Protobuf::Message&,
                       const std::string&) const override {}

protected:
  Server::Configuration::ListenerFactoryContext& factory_context_;
};

class TcpListenerDynamicFilterConfigProviderImpl
    : public ListenerDynamicFilterConfigProviderImpl<Network::ListenerFilterFactoryCb> {
public:
  using ListenerDynamicFilterConfigProviderImpl::ListenerDynamicFilterConfigProviderImpl;

private:
  Network::ListenerFilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message) const override {
    auto* factory =
        Registry::FactoryRegistry<Server::Configuration::NamedListenerFilterConfigFactory>::
            getFactoryByType(message.GetTypeName());
    return factory->createListenerFilterFactoryFromProto(message, listener_filter_matcher_,
                                                         factory_context_);
  }
};

class UdpListenerDynamicFilterConfigProviderImpl
    : public ListenerDynamicFilterConfigProviderImpl<Network::UdpListenerFilterFactoryCb> {
public:
  using ListenerDynamicFilterConfigProviderImpl::ListenerDynamicFilterConfigProviderImpl;

private:
  Network::UdpListenerFilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message) const override {
    auto* factory =
        Registry::FactoryRegistry<Server::Configuration::NamedUdpListenerFilterConfigFactory>::
            getFactoryByType(message.GetTypeName());
    return factory->createFilterFactoryFromProto(message, factory_context_);
  }
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
                           Server::Configuration::ServerFactoryContext& factory_context,
                           const std::string& stat_prefix,
                           FilterConfigProviderManagerImplBase& filter_config_provider_manager,
                           const std::string& subscription_id);

  ~FilterConfigSubscription() override;

  const Init::SharedTargetImpl& initTarget() { return init_target_; }
  const std::string& name() { return filter_config_name_; }
  const Protobuf::Message* lastConfig() { return last_config_.get(); }
  const std::string& lastTypeUrl() { return last_type_url_; }
  const std::string& lastVersionInfo() { return last_version_info_; }
  const std::string& lastFactoryName() { return last_factory_name_; }
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
  std::string last_factory_name_;
  Server::Configuration::ServerFactoryContext& factory_context_;

  Init::SharedTargetImpl init_target_;
  bool started_{false};

  Stats::ScopeSharedPtr scope_;
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
  OptRef<FactoryCb> config() override { return config_; }

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

  virtual std::tuple<ProtobufTypes::MessagePtr, std::string>
  getMessage(const envoy::config::core::v3::TypedExtensionConfig& filter_config,
             Server::Configuration::ServerFactoryContext& factory_context) const PURE;

protected:
  std::shared_ptr<FilterConfigSubscription> getSubscription(
      const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
      Server::Configuration::ServerFactoryContext& server_context, const std::string& stat_prefix);
  void applyLastOrDefaultConfig(std::shared_ptr<FilterConfigSubscription>& subscription,
                                DynamicFilterConfigProviderImplBase& provider,
                                const std::string& filter_config_name);
  void validateProtoConfigDefaultFactory(const bool null_default_factory,
                                         const std::string& filter_config_name,
                                         absl::string_view type_url) const;
  void validateProtoConfigTypeUrl(const std::string& type_url,
                                  const absl::flat_hash_set<std::string>& require_type_urls) const;

private:
  absl::flat_hash_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  friend class FilterConfigSubscription;
};

/**
 * An implementation of FilterConfigProviderManager.
 */
template <class Factory, class FactoryCb, class FactoryCtx, class DynamicFilterConfigImpl>
class FilterConfigProviderManagerImpl : public FilterConfigProviderManagerImplBase,
                                        public FilterConfigProviderManager<FactoryCb, FactoryCtx>,
                                        public Singleton::Instance {
public:
  DynamicFilterConfigProviderPtr<FactoryCb> createDynamicFilterConfigProvider(
      const envoy::config::core::v3::ExtensionConfigSource& config_source,
      const std::string& filter_config_name,
      Server::Configuration::ServerFactoryContext& server_context, FactoryCtx& factory_context,
      bool last_filter_in_filter_chain, const std::string& filter_chain_type,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher) override {
    std::string subscription_stat_prefix;
    absl::string_view provider_stat_prefix;
    subscription_stat_prefix =
        absl::StrCat("extension_config_discovery.", statPrefix(), filter_config_name, ".");
    provider_stat_prefix = subscription_stat_prefix;

    auto subscription = getSubscription(config_source.config_source(), filter_config_name,
                                        server_context, subscription_stat_prefix);
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
          getDefaultConfig(config_source.default_config(), filter_config_name, server_context,
                           last_filter_in_filter_chain, filter_chain_type, require_type_urls);
    }

    auto provider = createFilterConfigProviderImpl(subscription, require_type_urls, server_context,
                                                   factory_context, std::move(default_config),
                                                   last_filter_in_filter_chain, filter_chain_type,
                                                   provider_stat_prefix, listener_filter_matcher);

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

  absl::string_view statPrefix() const override PURE;

  std::tuple<ProtobufTypes::MessagePtr, std::string>
  getMessage(const envoy::config::core::v3::TypedExtensionConfig& filter_config,
             Server::Configuration::ServerFactoryContext& factory_context) const override {
    auto& factory = Config::Utility::getAndCheckFactory<Factory>(filter_config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        filter_config.typed_config(),
        factory_context.messageValidationContext().dynamicValidationVisitor(), factory);
    return {std::move(message), factory.name()};
  }

protected:
  virtual void validateFilters(const std::string&, const std::string&, const std::string&, bool,
                               bool) const {};
  virtual bool isTerminalFilter(Factory*, Protobuf::Message&,
                                Server::Configuration::ServerFactoryContext&) const {
    return false;
  }

  ProtobufTypes::MessagePtr
  getDefaultConfig(const ProtobufWkt::Any& proto_config, const std::string& filter_config_name,
                   Server::Configuration::ServerFactoryContext& server_context,
                   bool last_filter_in_filter_chain, const std::string& filter_chain_type,
                   const absl::flat_hash_set<std::string>& require_type_urls) const {
    auto* default_factory = Config::Utility::getFactoryByType<Factory>(proto_config);
    validateProtoConfigDefaultFactory(default_factory == nullptr, filter_config_name,
                                      proto_config.type_url());
    validateProtoConfigTypeUrl(Config::Utility::getFactoryType(proto_config), require_type_urls);
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        proto_config, server_context.messageValidationVisitor(), *default_factory);
    validateFilters(filter_config_name, default_factory->name(), filter_chain_type,
                    isTerminalFilter(default_factory, *message, server_context),
                    last_filter_in_filter_chain);
    return message;
  }

private:
  std::unique_ptr<DynamicFilterConfigProviderImpl<FactoryCb>> createFilterConfigProviderImpl(
      FilterConfigSubscriptionSharedPtr& subscription,
      const absl::flat_hash_set<std::string>& require_type_urls,
      Server::Configuration::ServerFactoryContext& server_context, FactoryCtx& factory_context,
      ProtobufTypes::MessagePtr&& default_config, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type, absl::string_view stat_prefix,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher) {
    return std::make_unique<DynamicFilterConfigImpl>(
        subscription, require_type_urls, server_context, factory_context, std::move(default_config),
        last_filter_in_filter_chain, filter_chain_type, stat_prefix, listener_filter_matcher);
  }
};

// HTTP filter
class HttpFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedHttpFilterConfigFactory, NamedHttpFilterFactoryCb,
          Server::Configuration::FactoryContext,
          HttpDynamicFilterConfigProviderImpl<
              Server::Configuration::FactoryContext,
              Server::Configuration::NamedHttpFilterConfigFactory>> {
public:
  absl::string_view statPrefix() const override { return "http_filter."; }

protected:
  bool
  isTerminalFilter(Server::Configuration::NamedHttpFilterConfigFactory* default_factory,
                   Protobuf::Message& message,
                   Server::Configuration::ServerFactoryContext& factory_context) const override {
    return default_factory->isTerminalFilterByProto(message, factory_context);
  }
  void validateFilters(const std::string& filter_config_name, const std::string& filter_type,
                       const std::string& filter_chain_type, bool is_terminal_filter,
                       bool last_filter_in_filter_chain) const override {
    Config::Utility::validateTerminalFilters(filter_config_name, filter_type, filter_chain_type,
                                             is_terminal_filter, last_filter_in_filter_chain);
  }
};

// HTTP filter
class UpstreamHttpFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::UpstreamHttpFilterConfigFactory, NamedHttpFilterFactoryCb,
          Server::Configuration::UpstreamHttpFactoryContext,
          HttpDynamicFilterConfigProviderImpl<
              Server::Configuration::UpstreamHttpFactoryContext,
              Server::Configuration::UpstreamHttpFilterConfigFactory>> {
public:
  absl::string_view statPrefix() const override { return "http_filter."; }

protected:
  bool
  isTerminalFilter(Server::Configuration::UpstreamHttpFilterConfigFactory* default_factory,
                   Protobuf::Message& message,
                   Server::Configuration::ServerFactoryContext& factory_context) const override {
    return default_factory->isTerminalFilterByProto(message, factory_context);
  }
  void validateFilters(const std::string& filter_config_name, const std::string& filter_type,
                       const std::string& filter_chain_type, bool is_terminal_filter,
                       bool last_filter_in_filter_chain) const override {
    Config::Utility::validateTerminalFilters(filter_config_name, filter_type, filter_chain_type,
                                             is_terminal_filter, last_filter_in_filter_chain);
  }
};

// TCP listener filter
class TcpListenerFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedListenerFilterConfigFactory, Network::ListenerFilterFactoryCb,
          Server::Configuration::ListenerFactoryContext,
          TcpListenerDynamicFilterConfigProviderImpl> {
public:
  absl::string_view statPrefix() const override { return "tcp_listener_filter."; }
};

// UDP listener filter
class UdpListenerFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedUdpListenerFilterConfigFactory,
          Network::UdpListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          UdpListenerDynamicFilterConfigProviderImpl> {
public:
  absl::string_view statPrefix() const override { return "udp_listener_filter."; }
};

} // namespace Filter
} // namespace Envoy
