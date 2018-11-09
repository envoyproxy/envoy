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

class ConfigProviderManagerImpl;

/**
 * ConfigProvider implementation for statically specified configuration.
 *
 * This class can not instantiated directly; instead, it provides the foundation for
 * static config provider implementations which derive from it.
 */
class StaticConfigProviderImpl : public ConfigProvider {
public:
  StaticConfigProviderImpl() = delete;

  virtual ~StaticConfigProviderImpl();

  // Envoy::Config::ConfigProvider
  SystemTime lastUpdated() const override { return last_updated_; }

protected:
  StaticConfigProviderImpl(Server::Configuration::FactoryContext& factory_context,
                           ConfigProviderManagerImpl& config_provider_manager);

private:
  SystemTime last_updated_;
  ConfigProviderManagerImpl& config_provider_manager_;
};

class DynamicConfigProviderImpl;

/**
 * Provides generic functionality required by all dynamic ConfigProvider subscriptions, including
 * shared lifetime management via shared_ptr.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config subscription implementations which derive from it.
 */
class ConfigSubscriptionInstance : public Init::Target,
                                   protected Logger::Loggable<Logger::Id::config> {
public:
  struct LastConfigInfo {
    uint64_t last_config_hash_;
    std::string last_config_version_;
  };

  ConfigSubscriptionInstance() = delete;

  virtual ~ConfigSubscriptionInstance();

  // Init::Target
  void initialize(std::function<void()> callback) override {
    initialize_callback_ = callback;
    start();
  }

  /**
   * Starts the subscription corresponding to a config source.
   * A derived class must manage the configuration proto specific Envoy::Config::Subscription to be
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

  const std::unordered_set<const DynamicConfigProviderImpl*> dynamic_config_providers() const {
    return std::unordered_set<const DynamicConfigProviderImpl*>(dynamic_config_providers_.begin(),
                                                                dynamic_config_providers_.end());
  }

protected:
  ConfigSubscriptionInstance(const std::string& name, const std::string& manager_identifier,
                             ConfigProviderManagerImpl& config_provider_manager,
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

  void bindConfigProvider(DynamicConfigProviderImpl* provider) {
    dynamic_config_providers_.insert(provider);
  }

  void unbindConfigProvider(DynamicConfigProviderImpl* provider) {
    dynamic_config_providers_.erase(provider);
  }

  const std::string name_;
  std::function<void()> initialize_callback_;
  std::unordered_set<DynamicConfigProviderImpl*> dynamic_config_providers_;
  const std::string manager_identifier_;
  ConfigProviderManagerImpl& config_provider_manager_;
  TimeSource& time_source_;
  SystemTime last_updated_;
  absl::optional<LastConfigInfo> config_info_;

  // Subscriptions, dynamic config providers and config provider managers are tightly coupled with
  // the current shared ownership model; use friend classes to explicitly denote the binding between
  // them.
  //
  // TODO(AndresGuedez): Investigate whether a shared ownership model avoiding the <shared_ptr>s and
  // instead centralizing lifetime management in the ConfigProviderManagerImpl with explicit
  // reference counting would be more maintainable.
  friend class DynamicConfigProviderImpl;
  friend class ConfigProviderManagerImpl;
};

using ConfigSubscriptionInstanceSharedPtr = std::shared_ptr<ConfigSubscriptionInstance>;

/**
 * Provides generic functionality required by all dynamic config providers, including distribution
 * of config updates to all workers.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config provider implementations which derive from it.
 */
class DynamicConfigProviderImpl : public ConfigProvider {
public:
  DynamicConfigProviderImpl() = delete;

  virtual ~DynamicConfigProviderImpl() { subscription_->unbindConfigProvider(this); }

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
  DynamicConfigProviderImpl(ConfigSubscriptionInstanceSharedPtr&& subscription,
                            Server::Configuration::FactoryContext& factory_context)
      : subscription_(subscription), tls_(factory_context.threadLocal().allocateSlot()) {}

  const ConfigSubscriptionInstanceSharedPtr& subscription() const { return subscription_; }

private:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    ThreadLocalConfig(ConfigProvider::ConfigConstSharedPtr initial_config)
        : config_(initial_config) {}

    ConfigProvider::ConfigConstSharedPtr config_;
  };

  ConfigSubscriptionInstanceSharedPtr subscription_;
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
class ConfigProviderManagerImpl : public ConfigProviderManager, public Singleton::Instance {
public:
  ConfigProviderManagerImpl() = delete;

  virtual ~ConfigProviderManagerImpl() {}

  /**
   * This is invoked by the /config_dump admin handler.
   * @return ProtobufTypes::MessagePtr the config dump proto corresponding to the associated
   *                                   {static,dynamic} config providers.
   */
  virtual ProtobufTypes::MessagePtr dumpConfigs() const PURE;

protected:
  using ConfigProviderSet = std::unordered_set<ConfigProvider*>;
  using ConfigSubscriptionMap =
      std::unordered_map<std::string, std::weak_ptr<ConfigSubscriptionInstance>>;

  ConfigProviderManagerImpl(Server::Admin& admin, const std::string& config_name);

  const ConfigProviderSet static_config_providers() const { return static_config_providers_; }

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
  std::shared_ptr<T>
  getSubscription(const Protobuf::Message& config_source_proto, Init::Manager& init_manager,
                  std::function<ConfigSubscriptionInstanceSharedPtr(const std::string&,
                                                                    ConfigProviderManagerImpl&)>
                      subscription_factory_fn) {
    static_assert(std::is_base_of<ConfigSubscriptionInstance, T>::value,
                  "T must be a subclass of ConfigSubscriptionInstance");

    ConfigSubscriptionInstanceSharedPtr subscription;
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
                        ConfigSubscriptionInstanceSharedPtr& subscription) {
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

  // See comment for friend classes in the ConfigSubscriptionInstance for more details on the use
  // of friends.
  friend class ConfigSubscriptionInstance;
  friend class StaticConfigProviderImpl;
};

} // namespace Config
} // namespace Envoy
