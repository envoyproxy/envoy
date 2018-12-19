#pragma once

#include <memory>

#include "envoy/config/config_provider.h"
#include "envoy/config/config_provider_manager.h"
#include "envoy/init/init.h"
#include "envoy/server/admin.h"
#include "envoy/server/config_tracker.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

// This file provides a set of base classes, (StaticConfigProviderImplBase,
// DynamicConfigProviderImplBase, ConfigProviderManagerImplBase, ConfigSubscriptionInstanceBase),
// conforming to the ConfigProvider/ConfigProviderManager interfaces, which in tandem provide a
// framework for implementing static and dynamic configuration for Envoy.
//
// Dynamic configuration is distributed via xDS APIs (see
// https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md). The framework exposed
// by these classes simplifies creation of client xDS implementations following a shared ownership
// model, where according to the config source specification, a config subscription, config protos
// received over the subscription and the subsequent config "implementation" (i.e., data structures
// and associated business logic) are shared across ConfigProvider objects and Envoy worker threads.
//
// This approach enables linear memory scalability based primarily on the size of the configuration
// set.
//
// A blueprint to follow for implementing static and dynamic config providers is as follows:
//
// For both:
//   1) Create a class derived from ConfigProviderManagerImplBase and implement the required
//   interface.
//      When implementing createXdsConfigProvider(), it is expected that getSubscription<T>() will
//      be called to fetch either an existing ConfigSubscriptionInstanceBase if the config source
//      configuration matches, or a newly instantiated subscription otherwise.
//
// For static providers:
//   1) Create a class derived from StaticConfigProviderImplBase and implement the required
//   interface.
//
// For dynamic (xDS) providers:
//   1) Create a class derived from DynamicConfigProviderImplBase and implement the required
//   interface. 2) Create a class derived from ConfigSubscriptionInstanceBase; this is the entity
//   responsible for owning and managing the Envoy::Config::Subscription<ConfigProto> that provides
//   the underlying config subscription.
//     - When subscription callbacks (onConfigUpdate, onConfigUpdateFailed) are issued by the
//     underlying subscription, the corresponding ConfigSubscriptionInstanceBase functions must be
//     called as well.
//     - On a successful config update, checkAndApplyConfig() should be called to instantiate the
//     new config implementation and propagate it to the shared config providers and all
//     worker threads.

class ConfigProviderManagerImplBase;

/**
 * ConfigProvider implementation for statically specified configuration.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * static config provider implementations which derive from it.
 */
class StaticConfigProviderImplBase : public ConfigProvider {
public:
  StaticConfigProviderImplBase() = delete;

  virtual ~StaticConfigProviderImplBase();

  // Envoy::Config::ConfigProvider
  SystemTime lastUpdated() const override { return last_updated_; }

protected:
  StaticConfigProviderImplBase(Server::Configuration::FactoryContext& factory_context,
                               ConfigProviderManagerImplBase& config_provider_manager);

private:
  SystemTime last_updated_;
  ConfigProviderManagerImplBase& config_provider_manager_;
};

class DynamicConfigProviderImplBase;

/**
 * Provides generic functionality required by all dynamic ConfigProvider subscriptions, including
 * shared lifetime management via shared_ptr.
 *
 * To do so, this class keeps track of a set of dynamic config providers associated with an
 * underlying subscription; providers are bound/unbound as needed as they are created and destroyed.
 * This is done to avoid exposing shared ownership semantics to the external API (see
 * ConfigProvider).
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config subscription implementations which derive from it.
 */
class ConfigSubscriptionInstanceBase : public Init::Target,
                                       protected Logger::Loggable<Logger::Id::config> {
public:
  struct LastConfigInfo {
    uint64_t last_config_hash_;
    std::string last_config_version_;
  };

  ConfigSubscriptionInstanceBase() = delete;

  virtual ~ConfigSubscriptionInstanceBase();

  // Init::Target
  void initialize(std::function<void()> callback) override {
    initialize_callback_ = callback;
    start();
  }

  /**
   * Starts the subscription corresponding to a config source.
   * A derived class must own the configuration proto specific Envoy::Config::Subscription to be
   * started.
   */
  virtual void start() PURE;

  const SystemTime& lastUpdated() const { return last_updated_; }

  const absl::optional<LastConfigInfo>& configInfo() const { return config_info_; }

  /**
   * Must be called by derived classes when the onConfigUpdate() callback associated with the
   * underlying subscription is issued.
   */
  void onConfigUpdate() {
    setLastUpdated();
    runInitializeCallbackIfAny();
  }

  /**
   * Must be called by derived classes when the onConfigUpdateFailed() callback associated with the
   * underlying subscription is issued.
   */
  void onConfigUpdateFailed() { runInitializeCallbackIfAny(); }

  /**
   * Determines whether a configuration proto is a new update, and if so, propagates it to all
   * config providers associated with this subscription.
   * @param config_proto supplies the newly received config proto.
   * @param config_name supplies the name associated with the config.
   * @param version_info supplies the version associated with the config.
   * @return bool false when the config proto has no delta from the previous config, true otherwise.
   */
  bool checkAndApplyConfig(const Protobuf::Message& config_proto, const std::string& config_name,
                           const std::string& version_info);

  const std::unordered_set<const DynamicConfigProviderImplBase*> dynamic_config_providers() const {
    return std::unordered_set<const DynamicConfigProviderImplBase*>(
        dynamic_config_providers_.begin(), dynamic_config_providers_.end());
  }

protected:
  ConfigSubscriptionInstanceBase(const std::string& name, const std::string& manager_identifier,
                                 ConfigProviderManagerImplBase& config_provider_manager,
                                 TimeSource& time_source, const SystemTime& last_updated,
                                 const LocalInfo::LocalInfo& local_info)
      : name_(name), manager_identifier_(manager_identifier),
        config_provider_manager_(config_provider_manager), time_source_(time_source),
        last_updated_(last_updated) {
    Envoy::Config::Utility::checkLocalInfo(name, local_info);
  }

  void setLastUpdated() { last_updated_ = time_source_.systemTime(); }

  void runInitializeCallbackIfAny();

private:
  void registerInitTarget(Init::Manager& init_manager) { init_manager.registerTarget(*this); }

  void bindConfigProvider(DynamicConfigProviderImplBase* provider);

  void unbindConfigProvider(DynamicConfigProviderImplBase* provider) {
    dynamic_config_providers_.erase(provider);
  }

  const std::string name_;
  std::function<void()> initialize_callback_;
  std::unordered_set<DynamicConfigProviderImplBase*> dynamic_config_providers_;
  const std::string manager_identifier_;
  ConfigProviderManagerImplBase& config_provider_manager_;
  TimeSource& time_source_;
  SystemTime last_updated_;
  absl::optional<LastConfigInfo> config_info_;

  // Subscriptions, dynamic config providers and config provider managers are tightly coupled with
  // the current shared ownership model; use friend classes to explicitly denote the binding between
  // them.
  //
  // TODO(AndresGuedez): Investigate whether a shared ownership model avoiding the <shared_ptr>s and
  // instead centralizing lifetime management in the ConfigProviderManagerImplBase with explicit
  // reference counting would be more maintainable.
  friend class DynamicConfigProviderImplBase;
  friend class ConfigProviderManagerImplBase;
};

using ConfigSubscriptionInstanceBaseSharedPtr = std::shared_ptr<ConfigSubscriptionInstanceBase>;

/**
 * Provides generic functionality required by all dynamic config providers, including distribution
 * of config updates to all workers.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config provider implementations which derive from it.
 */
class DynamicConfigProviderImplBase : public ConfigProvider {
public:
  DynamicConfigProviderImplBase() = delete;

  virtual ~DynamicConfigProviderImplBase() { subscription_->unbindConfigProvider(this); }

  // Envoy::Config::ConfigProvider
  SystemTime lastUpdated() const override { return subscription_->lastUpdated(); }

  // Envoy::Config::ConfigProvider
  ConfigConstSharedPtr getConfig() const override {
    return tls_->getTyped<ThreadLocalConfig>().config_;
  }

  virtual ConfigConstSharedPtr onConfigProtoUpdate(const Protobuf::Message& config_proto) PURE;

  /**
   * Must be called by the derived class' constructor.
   * @param initial_config supplies an initial Envoy::Config::ConfigProvider::Config associated with
   *                       the underlying subscription.
   */
  void initialize(ConfigConstSharedPtr initial_config) {
    subscription_->bindConfigProvider(this);
    tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalConfig>(initial_config);
    });
  }

  /**
   * Propagates a newly instantiated Envoy::Config::ConfigProvider::Config to all workers.
   * @param config supplies the newly instantiated config.
   */
  void onConfigUpdate(ConfigConstSharedPtr config) {
    tls_->runOnAllThreads(
        [this, config]() -> void { tls_->getTyped<ThreadLocalConfig>().config_ = config; });
  }

protected:
  DynamicConfigProviderImplBase(ConfigSubscriptionInstanceBaseSharedPtr&& subscription,
                                Server::Configuration::FactoryContext& factory_context)
      : subscription_(subscription), tls_(factory_context.threadLocal().allocateSlot()) {}

  const ConfigSubscriptionInstanceBaseSharedPtr& subscription() const { return subscription_; }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigProvider::ConfigConstSharedPtr initial_config)
        : config_(initial_config) {}

    ConfigProvider::ConfigConstSharedPtr config_;
  };

  ConfigSubscriptionInstanceBaseSharedPtr subscription_;
  ThreadLocal::SlotPtr tls_;
};

/**
 * Provides generic functionality required by all config provider managers, such as managing shared
 * lifetime of subscriptions and dynamic config providers, along with determining which
 * subscriptions should be associated with newly instantiated providers.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config provider implementations which derive from it.
 */
class ConfigProviderManagerImplBase : public ConfigProviderManager, public Singleton::Instance {
public:
  ConfigProviderManagerImplBase() = delete;

  virtual ~ConfigProviderManagerImplBase() {}

  /**
   * This is invoked by the /config_dump admin handler.
   * @return ProtobufTypes::MessagePtr the config dump proto corresponding to the associated
   *                                   {static,dynamic} config providers.
   */
  virtual ProtobufTypes::MessagePtr dumpConfigs() const PURE;

protected:
  using ConfigProviderSet = std::unordered_set<ConfigProvider*>;
  using ConfigSubscriptionMap =
      std::unordered_map<std::string, std::weak_ptr<ConfigSubscriptionInstanceBase>>;

  ConfigProviderManagerImplBase(Server::Admin& admin, const std::string& config_name);

  const ConfigProviderSet& static_config_providers() const { return static_config_providers_; }

  const ConfigSubscriptionMap& config_subscriptions() const { return config_subscriptions_; }

  /**
   * Returns the subscription associated with the config_source_proto; if none exists, a new one is
   * allocated according to the subscription_factory_fn.
   * @param config_source_proto supplies the proto specifying the config subscription paremeters.
   * @param init_manager supplies the init manager.
   * @param subscription_factory_fn supplies a function to be called when a new subscription needs
   *                                to be allocated.
   * @return std::shared_ptr<T> an existing (if a match is found) or newly allocated subscription.
   */
  template <typename T>
  std::shared_ptr<T> getSubscription(const Protobuf::Message& config_source_proto,
                                     Init::Manager& init_manager,
                                     std::function<ConfigSubscriptionInstanceBaseSharedPtr(
                                         const std::string&, ConfigProviderManagerImplBase&)>
                                         subscription_factory_fn) {
    static_assert(std::is_base_of<ConfigSubscriptionInstanceBase, T>::value,
                  "T must be a subclass of ConfigSubscriptionInstanceBase");

    ConfigSubscriptionInstanceBaseSharedPtr subscription;
    const std::string manager_identifier = config_source_proto.SerializeAsString();

    auto it = config_subscriptions_.find(manager_identifier);
    if (it == config_subscriptions_.end()) {
      // std::make_shared does not work for classes with private constructors. There are ways
      // around it. However, since this is not a performance critical path we err on the side
      // of simplicity.
      subscription = subscription_factory_fn(manager_identifier, *this);

      subscription->registerInitTarget(init_manager);

      bindSubscription(manager_identifier, subscription);
    } else {
      // Because the RouteConfigProviderManager's weak_ptrs only get cleaned up
      // in the RdsRouteConfigSubscription destructor, and the single threaded nature
      // of this code, locking the weak_ptr will not fail.
      subscription = it->second.lock();
    }
    ASSERT(subscription);

    return std::static_pointer_cast<T>(subscription);
  }

private:
  void bindSubscription(const std::string& manager_identifier,
                        ConfigSubscriptionInstanceBaseSharedPtr& subscription) {
    config_subscriptions_.insert({manager_identifier, subscription});
  }

  void unbindSubscription(const std::string& manager_identifier) {
    config_subscriptions_.erase(manager_identifier);
  }

  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  ConfigSubscriptionMap config_subscriptions_;
  ConfigProviderSet static_config_providers_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;

  // See comment for friend classes in the ConfigSubscriptionInstanceBase for more details on the
  // use of friends.
  friend class ConfigSubscriptionInstanceBase;
  friend class StaticConfigProviderImplBase;
};

} // namespace Config
} // namespace Envoy
