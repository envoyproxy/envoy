#pragma once

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/config/subscription.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/http/filter.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/admin.h"
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

  absl::Status validateTypeUrl(const std::string& type_url) const;
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
        main_config_(std::make_shared<MainConfig>(tls)),
        default_configuration_(std::move(default_config)){};

  ~DynamicFilterConfigProviderImpl() override {
    auto& tls = main_config_->tls_;
    if (!tls->isShutdown()) {
      tls->runOnAllThreads([](OptRef<ThreadLocalConfig> tls) { tls->config_ = {}; },
                           // Extend the lifetime of TLS by capturing main_config_, because
                           // otherwise, the callback to clear TLS worker content is not executed.
                           [main_config = main_config_]() {
                             // Explicitly delete TLS on the main thread.
                             main_config->tls_.reset();
                             // Explicitly clear the last config instance here in case it has its
                             // own TLS.
                             main_config->current_config_ = {};
                           });
    }
  }

  // Config::ExtensionConfigProvider
  const std::string& name() override { return DynamicFilterConfigProviderImplBase::name(); }
  OptRef<FactoryCb> config() override {
    if (auto& optional_config = (*main_config_->tls_)->config_; optional_config.has_value()) {
      return optional_config.value();
    }
    return {};
  }

  // Config::DynamicExtensionConfigProviderBase
  absl::Status onConfigUpdate(const Protobuf::Message& message, const std::string&,
                              Config::ConfigAppliedCb applied_on_all_threads) override {
    const FactoryCb config = instantiateFilterFactory(message);
    update(config, applied_on_all_threads);
    return absl::OkStatus();
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
      auto status = onConfigUpdate(*default_configuration_, "", nullptr);
      if (!status.ok()) {
        throwEnvoyExceptionOrPanic(std::string(status.message()));
      }
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
    main_config_->tls_->runOnAllThreads(
        [config](OptRef<ThreadLocalConfig> tls) { tls->config_ = config; },
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
    MainConfig(ThreadLocal::SlotAllocator& tls)
        : tls_(std::make_unique<ThreadLocal::TypedSlot<ThreadLocalConfig>>(tls)) {
      tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalConfig>(); });
    }
    absl::optional<FactoryCb> current_config_{absl::nullopt};
    ThreadLocal::TypedSlotPtr<ThreadLocalConfig> tls_;
  };
  const std::string stat_prefix_;
  std::shared_ptr<MainConfig> main_config_;
  const ProtobufTypes::MessagePtr default_configuration_;
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
    absl::StatusOr<Http::FilterFactoryCb> error_or_factory =
        factory->createFilterFactoryFromProto(message, getStatPrefix(), factory_context_);
    if (!error_or_factory.status().ok()) {
      throwEnvoyExceptionOrPanic(std::string(error_or_factory.status().message()));
    }

    return {factory->name(), error_or_factory.value()};
  }

  Server::Configuration::ServerFactoryContext& server_context_;
  FactoryCtx& factory_context_;
};

template <class FactoryCtx, class NeutralNetworkFilterConfigFactory>
class NetworkDynamicFilterConfigProviderImplBase
    : public DynamicFilterConfigProviderImpl<Network::FilterFactoryCb> {
public:
  NetworkDynamicFilterConfigProviderImplBase(
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

private:
  Network::FilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message) const override {
    auto* factory = Registry::FactoryRegistry<NeutralNetworkFilterConfigFactory>::getFactoryByType(
        message.GetTypeName());
    return factory->createFilterFactoryFromProto(message, factory_context_);
  }

protected:
  Server::Configuration::ServerFactoryContext& server_context_;
  FactoryCtx& factory_context_;
};

template <class FactoryCtx, class NeutralNetworkFilterConfigFactory>
class DownstreamNetworkDynamicFilterConfigProviderImpl
    : public NetworkDynamicFilterConfigProviderImplBase<FactoryCtx,
                                                        NeutralNetworkFilterConfigFactory> {
public:
  using NetworkDynamicFilterConfigProviderImplBase<
      FactoryCtx, NeutralNetworkFilterConfigFactory>::NetworkDynamicFilterConfigProviderImplBase;

  void validateMessage(const std::string& config_name, const Protobuf::Message& message,
                       const std::string& factory_name) const override {
    auto* factory =
        Registry::FactoryRegistry<NeutralNetworkFilterConfigFactory>::getFactory(factory_name);
    const bool is_terminal_filter =
        factory->isTerminalFilterByProto(message, this->server_context_);
    Config::Utility::validateTerminalFilters(config_name, factory_name, this->filter_chain_type_,
                                             is_terminal_filter,
                                             this->last_filter_in_filter_chain_);
  }
};

template <class FactoryCtx, class NeutralNetworkFilterConfigFactory>
class UpstreamNetworkDynamicFilterConfigProviderImpl
    : public NetworkDynamicFilterConfigProviderImplBase<FactoryCtx,
                                                        NeutralNetworkFilterConfigFactory> {
public:
  using NetworkDynamicFilterConfigProviderImplBase<
      FactoryCtx, NeutralNetworkFilterConfigFactory>::NetworkDynamicFilterConfigProviderImplBase;

  void validateMessage(const std::string&, const Protobuf::Message&,
                       const std::string&) const override {
    // Upstream network filters don't use the concept of terminal filters.
  }
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

class QuicListenerDynamicFilterConfigProviderImpl
    : public ListenerDynamicFilterConfigProviderImpl<Network::QuicListenerFilterFactoryCb> {
public:
  using ListenerDynamicFilterConfigProviderImpl::ListenerDynamicFilterConfigProviderImpl;

private:
  Network::QuicListenerFilterFactoryCb
  instantiateFilterFactory(const Protobuf::Message& message) const override {
    auto* factory =
        Registry::FactoryRegistry<Server::Configuration::NamedQuicListenerFilterConfigFactory>::
            getFactoryByType(message.GetTypeName());
    return factory->createListenerFilterFactoryFromProto(message, listener_filter_matcher_,
                                                         factory_context_);
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
      Logger::Loggable<Logger::Id::filter>,
      public std::enable_shared_from_this<FilterConfigSubscription> {
public:
  FilterConfigSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                           const std::string& filter_config_name,
                           Server::Configuration::ServerFactoryContext& factory_context,
                           Upstream::ClusterManager& cluster_manager,
                           const std::string& stat_prefix,
                           FilterConfigProviderManagerImplBase& filter_config_provider_manager,
                           const std::string& subscription_id);

  ~FilterConfigSubscription() override;

  const Init::SharedTargetImpl& initTarget() { return init_target_; }
  const std::string& name() { return filter_config_name_; }
  const Protobuf::Message* lastConfig() { return last_->config_.get(); }
  const std::string& lastTypeUrl() { return last_->type_url_; }
  const std::string& lastVersionInfo() { return last_->version_info_; }
  const std::string& lastFactoryName() { return last_->factory_name_; }
  const SystemTime& lastUpdated() { return last_->updated_; }

  void incrementConflictCounter();

private:
  struct ConfigVersion {
    ConfigVersion(const std::string& version_info, SystemTime updated)
        : version_info_(version_info), updated_(updated) {}
    ProtobufTypes::MessagePtr config_;
    std::string type_url_;
    const std::string version_info_;
    std::string factory_name_;
    uint64_t config_hash_{0ul};
    const SystemTime updated_;
  };
  // Using a heap allocated record because updates are completed by all workers asynchronously.
  using ConfigVersionSharedPtr = std::shared_ptr<ConfigVersion>;
  using ConfigVersionConstSharedPtr = std::shared_ptr<const ConfigVersion>;

  void start();

  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string&) override;
  void onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                            const EnvoyException*) override;
  void updateComplete();

  const std::string filter_config_name_;
  ConfigVersionConstSharedPtr last_;
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
  std::shared_ptr<FilterConfigSubscription>
  getSubscription(const envoy::config::core::v3::ConfigSource& config_source,
                  const std::string& name,
                  Server::Configuration::ServerFactoryContext& server_context,
                  Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix);
  void applyLastOrDefaultConfig(std::shared_ptr<FilterConfigSubscription>& subscription,
                                DynamicFilterConfigProviderImplBase& provider,
                                const std::string& filter_config_name);
  void validateProtoConfigDefaultFactory(const bool null_default_factory,
                                         const std::string& filter_config_name,
                                         absl::string_view type_url) const;
  void validateProtoConfigTypeUrl(const std::string& type_url,
                                  const absl::flat_hash_set<std::string>& require_type_urls) const;
  // Return the config dump map key string for the corresponding ECDS filter type.
  virtual const std::string getConfigDumpType() const PURE;

private:
  void setupEcdsConfigDumpCallbacks(OptRef<Server::Admin> admin) {
    if (admin.has_value()) {
      if (config_tracker_entry_ == nullptr) {
        config_tracker_entry_ = admin->getConfigTracker().add(
            getConfigDumpType(), [this](const Matchers::StringMatcher& name_matcher) {
              return dumpEcdsFilterConfigs(name_matcher);
            });
      }
    }
  }

  ProtobufTypes::MessagePtr dumpEcdsFilterConfigs(const Matchers::StringMatcher& name_matcher) {
    auto config_dump = std::make_unique<envoy::admin::v3::EcdsConfigDump>();
    for (const auto& subscription : subscriptions_) {
      const auto& ecds_filter = subscription.second.lock();
      if (!ecds_filter || !name_matcher.match(ecds_filter->name())) {
        continue;
      }
      envoy::config::core::v3::TypedExtensionConfig filter_config;
      filter_config.set_name(ecds_filter->name());
      if (ecds_filter->lastConfig()) {
        MessageUtil::packFrom(*filter_config.mutable_typed_config(), *ecds_filter->lastConfig());
      }
      auto& filter_config_dump = *config_dump->mutable_ecds_filters()->Add();
      filter_config_dump.mutable_ecds_filter()->PackFrom(filter_config);
      filter_config_dump.set_version_info(ecds_filter->lastVersionInfo());
      TimestampUtil::systemClockToTimestamp(ecds_filter->lastUpdated(),
                                            *(filter_config_dump.mutable_last_updated()));
    }
    return config_dump;
  }

  absl::flat_hash_map<std::string, std::weak_ptr<FilterConfigSubscription>> subscriptions_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
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
      Upstream::ClusterManager& cluster_manager, bool last_filter_in_filter_chain,
      const std::string& filter_chain_type,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher) override {
    std::string subscription_stat_prefix;
    absl::string_view provider_stat_prefix;
    subscription_stat_prefix =
        absl::StrCat("extension_config_discovery.", statPrefix(), filter_config_name, ".");
    provider_stat_prefix = subscription_stat_prefix;

    auto subscription = getSubscription(config_source.config_source(), filter_config_name,
                                        server_context, cluster_manager, subscription_stat_prefix);
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

    std::unique_ptr<DynamicFilterConfigProviderImpl<FactoryCb>> provider =
        std::make_unique<DynamicFilterConfigImpl>(subscription, require_type_urls, server_context,
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
  const std::string getConfigDumpType() const override { return "ecds_filter_http"; }
};

// HTTP filter
class UpstreamHttpFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::UpstreamHttpFilterConfigFactory, NamedHttpFilterFactoryCb,
          Server::Configuration::UpstreamFactoryContext,
          HttpDynamicFilterConfigProviderImpl<
              Server::Configuration::UpstreamFactoryContext,
              Server::Configuration::UpstreamHttpFilterConfigFactory>> {
public:
  absl::string_view statPrefix() const override { return "upstream_http_filter."; }

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
  const std::string getConfigDumpType() const override { return "ecds_filter_upstream_http"; }
};

// Network filter
class NetworkFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedNetworkFilterConfigFactory, Network::FilterFactoryCb,
          Server::Configuration::FactoryContext,
          DownstreamNetworkDynamicFilterConfigProviderImpl<
              Server::Configuration::FactoryContext,
              Server::Configuration::NamedNetworkFilterConfigFactory>> {
public:
  absl::string_view statPrefix() const override { return "network_filter."; }

protected:
  bool
  isTerminalFilter(Server::Configuration::NamedNetworkFilterConfigFactory* default_factory,
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
  const std::string getConfigDumpType() const override { return "ecds_filter_network"; }
};

// Upstream network filter
class UpstreamNetworkFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedUpstreamNetworkFilterConfigFactory, Network::FilterFactoryCb,
          Server::Configuration::UpstreamFactoryContext,
          UpstreamNetworkDynamicFilterConfigProviderImpl<
              Server::Configuration::UpstreamFactoryContext,
              Server::Configuration::NamedUpstreamNetworkFilterConfigFactory>> {
public:
  absl::string_view statPrefix() const override { return "upstream_network_filter."; }

protected:
  bool
  isTerminalFilter(Server::Configuration::NamedUpstreamNetworkFilterConfigFactory* default_factory,
                   Protobuf::Message& message,
                   Server::Configuration::ServerFactoryContext& factory_context) const override {
    return default_factory->isTerminalFilterByProto(message, factory_context);
  }
  void validateFilters(const std::string&, const std::string&, const std::string&, bool,
                       bool) const override {
    // Upstream network filters don't use the concept of terminal filters.
  }
  const std::string getConfigDumpType() const override { return "ecds_filter_upstream_network"; }
};

// TCP listener filter
class TcpListenerFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedListenerFilterConfigFactory, Network::ListenerFilterFactoryCb,
          Server::Configuration::ListenerFactoryContext,
          TcpListenerDynamicFilterConfigProviderImpl> {
public:
  absl::string_view statPrefix() const override { return "tcp_listener_filter."; }

protected:
  const std::string getConfigDumpType() const override { return "ecds_filter_tcp_listener"; }
};

// UDP listener filter
class UdpListenerFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedUdpListenerFilterConfigFactory,
          Network::UdpListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          UdpListenerDynamicFilterConfigProviderImpl> {
public:
  absl::string_view statPrefix() const override { return "udp_listener_filter."; }

protected:
  const std::string getConfigDumpType() const override { return "ecds_filter_udp_listener"; }
};

// QUIC listener filter
class QuicListenerFilterConfigProviderManagerImpl
    : public FilterConfigProviderManagerImpl<
          Server::Configuration::NamedQuicListenerFilterConfigFactory,
          Network::QuicListenerFilterFactoryCb, Server::Configuration::ListenerFactoryContext,
          QuicListenerDynamicFilterConfigProviderImpl> {
public:
  absl::string_view statPrefix() const override { return "quic_listener_filter."; }

protected:
  const std::string getConfigDumpType() const override { return "ecds_filter_quic_listener"; }
};

} // namespace Filter
} // namespace Envoy
