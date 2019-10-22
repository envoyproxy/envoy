#pragma once

#include "envoy/config/config_provider.h"
#include "envoy/config/config_provider_manager.h"
#include "envoy/init/manager.h"
#include "envoy/server/admin.h"
#include "envoy/server/config_tracker.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/thread.h"
#include "common/common/utility.h"
#include "common/config/utility.h"
#include "common/init/target_impl.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

// This file provides a set of base classes, (ImmutableConfigProviderBase,
// MutableConfigProviderCommonBase, ConfigProviderManagerImplBase, ConfigSubscriptionCommonBase,
// ConfigSubscriptionInstance, DeltaConfigSubscriptionInstance), conforming to the
// ConfigProvider/ConfigProviderManager interfaces, which in tandem provide a framework for
// implementing statically defined (i.e., immutable) and dynamic (mutable via subscriptions)
// configuration for Envoy.
//
// The mutability property applies to the ConfigProvider itself and _not_ the underlying config
// proto, which is always immutable. MutableConfigProviderCommonBase objects receive config proto
// updates via xDS subscriptions, resulting in new ConfigProvider::Config objects being instantiated
// with the corresponding change in behavior corresponding to updated config. ConfigProvider::Config
// objects must be latched/associated with the appropriate objects in the connection and request
// processing pipeline, such that configuration stays consistent for the lifetime of the connection
// and/or stream/request (if required by the configuration being processed).
//
// Dynamic configuration is distributed via xDS APIs (see
// https://github.com/envoyproxy/data-plane-api/blob/master/xds_protocol.rst). The framework exposed
// by these classes simplifies creation of client xDS implementations following a shared ownership
// model, where according to the config source specification, a config subscription, config protos
// received over the subscription and the subsequent config "implementation" (i.e., data structures
// and associated business logic) are shared across ConfigProvider objects and Envoy worker threads.
//
// This approach enables linear memory scalability based primarily on the size of the configuration
// set.
//
// A blueprint to follow for implementing mutable or immutable config providers is as follows:
//
// For both:
//   1) Create a class derived from ConfigProviderManagerImplBase and implement the required
//   interface.
//      When implementing createXdsConfigProvider(), it is expected that getSubscription<T>() will
//      be called to fetch either an existing ConfigSubscriptionCommonBase if the config
//      source configuration matches, or a newly instantiated subscription otherwise.
//
// For immutable providers:
//   1) Create a class derived from ImmutableConfigProviderBase and implement the required
//   interface.
//
// For mutable (xDS) providers:
//   1) According to the API type, create a class derived from MutableConfigProviderCommonBase and
//   implement the required interface.
//   2) According to the API type, create a class derived from
//   ConfigSubscriptionInstance or DeltaConfigSubscriptionInstance; this is the entity responsible
//   for owning and managing the Envoy::Config::Subscription<ConfigProto> that provides the
//   underlying config subscription, and the Config implementation shared by associated providers.
//     a) For a ConfigProvider::ApiType::Full subscription instance (i.e., a
//     ConfigSubscriptionInstance child):
//     - When subscription callbacks (onConfigUpdate, onConfigUpdateFailed) are issued by the
//     underlying subscription, the corresponding ConfigSubscriptionInstance functions
//     must be called as well.
//     - On a successful config update, checkAndApplyConfigUpdate() should be called to instantiate
//     the new config implementation and propagate it to the shared config providers and all worker
//     threads.
//       - On a successful return from checkAndApplyConfigUpdate(), the config proto must be latched
//       into this class and returned via the getConfigProto() override.
//    b) For a ConfigProvider::ApiType::Delta subscription instance (i.e., a
//    DeltaConfigSubscriptionInstance child):
//    - When subscription callbacks (onConfigUpdate, onConfigUpdateFailed) are issued by the
//    underlying subscription, the corresponding ConfigSubscriptionInstance functions must be called
//    as well.
//    - On a successful config update, applyConfigUpdate() should be called to propagate the
//    config updates to all bound config providers and worker threads.

class ConfigProviderManagerImplBase;

/**
 * Specifies the type of config associated with a ConfigProvider.
 */
enum class ConfigProviderInstanceType {
  // Configuration defined as a static resource in the bootstrap config.
  Static,
  // Configuration defined inline in a resource that may be specified statically or obtained via
  // xDS.
  Inline,
  // Configuration obtained from an xDS subscription.
  Xds
};

/**
 * ConfigProvider implementation for immutable configuration.
 *
 * TODO(AndresGuedez): support sharing of config protos and config impls, as is
 * done with the MutableConfigProviderCommonBase.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * immutable config provider implementations which derive from it.
 */
class ImmutableConfigProviderBase : public ConfigProvider {
public:
  ~ImmutableConfigProviderBase() override;

  // Envoy::Config::ConfigProvider
  SystemTime lastUpdated() const override { return last_updated_; }
  ApiType apiType() const override { return api_type_; }

  ConfigProviderInstanceType instanceType() const { return instance_type_; }

protected:
  ImmutableConfigProviderBase(Server::Configuration::FactoryContext& factory_context,
                              ConfigProviderManagerImplBase& config_provider_manager,
                              ConfigProviderInstanceType instance_type, ApiType api_type);

private:
  SystemTime last_updated_;
  ConfigProviderManagerImplBase& config_provider_manager_;
  ConfigProviderInstanceType instance_type_;
  ApiType api_type_;
};

class MutableConfigProviderCommonBase;

/**
 * Provides common DS API subscription functionality required by the ConfigProvider::ApiType.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * config subscription implementations which derive from it.
 *
 * A subscription is intended to be co-owned by config providers with the same config source, it's
 * designed to be created/destructed on admin thread only.
 *
 * xDS config providers and subscriptions are split to avoid lifetime issues with arguments
 * required by the config providers. An example is the Server::Configuration::FactoryContext, which
 * is owned by listeners and therefore may be destroyed while an associated config provider is still
 * in use (see #3960). This split enables single ownership of the config providers, while enabling
 * shared ownership of the underlying subscription.
 *
 */
class ConfigSubscriptionCommonBase : protected Logger::Loggable<Logger::Id::config> {
public:
  // Callback for updating a Config implementation held in each worker thread, the callback is
  // called in applyConfigUpdate() with the current version Config, and is expected to return the
  // new version Config.
  using ConfigUpdateCb =
      std::function<ConfigProvider::ConfigConstSharedPtr(ConfigProvider::ConfigConstSharedPtr)>;

  struct LastConfigInfo {
    absl::optional<uint64_t> last_config_hash_;
    std::string last_config_version_;
  };

  virtual ~ConfigSubscriptionCommonBase();

  /**
   * Starts the subscription corresponding to a config source.
   * A derived class must own the configuration proto specific Envoy::Config::Subscription to be
   * started.
   */
  virtual void start() PURE;

  const SystemTime& lastUpdated() const { return last_updated_; }

  const absl::optional<LastConfigInfo>& configInfo() const { return config_info_; }

  ConfigProvider::ConfigConstSharedPtr getConfig() const {
    return tls_->getTyped<ThreadLocalConfig>().config_;
  }

  /**
   * Must be called by derived classes when the onConfigUpdate() callback associated with the
   * underlying subscription is issued.
   */
  void onConfigUpdate() {
    setLastUpdated();
    init_target_.ready();
  }

  /**
   * Must be called by derived classes when the onConfigUpdateFailed() callback associated with the
   * underlying subscription is issued.
   */
  void onConfigUpdateFailed() {
    setLastUpdated();
    init_target_.ready();
  }

protected:
  struct ThreadLocalConfig : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalConfig(ConfigProvider::ConfigConstSharedPtr initial_config)
        : config_(std::move(initial_config)) {}

    ConfigProvider::ConfigConstSharedPtr config_;
  };

  ConfigSubscriptionCommonBase(const std::string& name, const uint64_t manager_identifier,
                               ConfigProviderManagerImplBase& config_provider_manager,
                               Server::Configuration::FactoryContext& factory_context)
      : name_(name), tls_(factory_context.threadLocal().allocateSlot()),
        init_target_(fmt::format("ConfigSubscriptionCommonBase {}", name_), [this]() { start(); }),
        manager_identifier_(manager_identifier), config_provider_manager_(config_provider_manager),
        time_source_(factory_context.timeSource()),
        last_updated_(factory_context.timeSource().systemTime()) {
    Envoy::Config::Utility::checkLocalInfo(name, factory_context.localInfo());
  }

  /**
   * Propagates a config update to worker threads.
   *
   * @param update_fn the callback to run on each thread, it takes the previous version Config and
   * returns a updated/new version Config.
   */
  void applyConfigUpdate(const ConfigUpdateCb& update_fn);

  void setLastUpdated() { last_updated_ = time_source_.systemTime(); }

  void setLastConfigInfo(absl::optional<LastConfigInfo>&& config_info) {
    config_info_ = std::move(config_info);
  }

  const std::string name_;
  absl::optional<LastConfigInfo> config_info_;
  // This slot holds a Config implementation in each thread, which is intended to be shared between
  // config providers from the same config source.
  ThreadLocal::SlotPtr tls_;

private:
  Init::TargetImpl init_target_;
  const uint64_t manager_identifier_;
  ConfigProviderManagerImplBase& config_provider_manager_;
  TimeSource& time_source_;
  SystemTime last_updated_;

  // ConfigSubscriptionCommonBase, MutableConfigProviderCommonBase and
  // ConfigProviderManagerImplBase are tightly coupled with the current shared ownership model; use
  // friend classes to explicitly denote the binding between them.
  //
  // TODO(AndresGuedez): Investigate whether a shared ownership model avoiding the <shared_ptr>s and
  // instead centralizing lifetime management in the ConfigProviderManagerImplBase with explicit
  // reference counting would be more maintainable.
  friend class ConfigProviderManagerImplBase;
};

using ConfigSubscriptionCommonBaseSharedPtr = std::shared_ptr<ConfigSubscriptionCommonBase>;

/**
 * Provides common subscription functionality required by ConfigProvider::ApiType::Full DS APIs.
 * A single Config instance is shared across all providers and all workers associated with this
 * subscription.
 */
class ConfigSubscriptionInstance : public ConfigSubscriptionCommonBase {
public:
  ConfigSubscriptionInstance(const std::string& name, const uint64_t manager_identifier,
                             ConfigProviderManagerImplBase& config_provider_manager,
                             Server::Configuration::FactoryContext& factory_context)
      : ConfigSubscriptionCommonBase(name, manager_identifier, config_provider_manager,
                                     factory_context) {}

  /**
   * Must be called by the derived class' constructor.
   * @param initial_config supplies an initial Envoy::Config::ConfigProvider::Config associated
   * with the underlying subscription, shared across all providers and workers.
   */
  void initialize(const ConfigProvider::ConfigConstSharedPtr& initial_config) {
    tls_->set([initial_config](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalConfig>(initial_config);
    });
  }

  /**
   * Determines whether a configuration proto is a new update, and if so, propagates it to all
   * config providers associated with this subscription.
   * @param config_proto supplies the newly received config proto.
   * @param config_name supplies the name associated with the config.
   * @param version_info supplies the version associated with the config.
   * @return bool false when the config proto has no delta from the previous config, true
   * otherwise.
   */
  bool checkAndApplyConfigUpdate(const Protobuf::Message& config_proto,
                                 const std::string& config_name, const std::string& version_info);

protected:
  /**
   * Called when a new config proto is received via an xDS subscription.
   * On successful validation of the config, must return a shared_ptr to a ConfigProvider::Config
   * implementation that will be propagated to all mutable config providers sharing the
   * subscription.
   * Note that this function is called _once_ across all shared config providers per xDS
   * subscription config update.
   * @param config_proto supplies the configuration proto.
   * @return ConfigConstSharedPtr the ConfigProvider::Config to share with other providers.
   */
  virtual ConfigProvider::ConfigConstSharedPtr
  onConfigProtoUpdate(const Protobuf::Message& config_proto) PURE;
};

/**
 * Provides common subscription functionality required by ConfigProvider::ApiType::Delta DS APIs.
 */
class DeltaConfigSubscriptionInstance : public ConfigSubscriptionCommonBase {
protected:
  using ConfigSubscriptionCommonBase::ConfigSubscriptionCommonBase;

  /**
   * Must be called by the derived class' constructor.
   * @param init_cb supplies an initial Envoy::Config::ConfigProvider::Config associated with the
   * underlying subscription for each worker thread.
   */
  void initialize(const std::function<ConfigProvider::ConfigConstSharedPtr()>& init_cb) {
    tls_->set([init_cb](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalConfig>(init_cb());
    });
  }
};

/**
 * Provides generic functionality required by the ConfigProvider::ApiType specific dynamic config
 * providers.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config provider implementations which derive from it.
 */
class MutableConfigProviderCommonBase : public ConfigProvider {
public:
  // Envoy::Config::ConfigProvider
  SystemTime lastUpdated() const override { return subscription_->lastUpdated(); }
  ApiType apiType() const override { return api_type_; }

protected:
  MutableConfigProviderCommonBase(ConfigSubscriptionCommonBaseSharedPtr&& subscription,
                                  ApiType api_type)
      : subscription_(subscription), api_type_(api_type) {}

  // Envoy::Config::ConfigProvider
  ConfigConstSharedPtr getConfig() const override { return subscription_->getConfig(); }

  ConfigSubscriptionCommonBaseSharedPtr subscription_;

private:
  ApiType api_type_;
};

/**
 * Provides generic functionality required by all config provider managers, such as managing
 * shared lifetime of subscriptions and dynamic config providers, along with determining which
 * subscriptions should be associated with newly instantiated providers.
 *
 * The implementation of this class is not thread safe. Note that ImmutableConfigProviderBase
 * and ConfigSubscriptionCommonBase call the corresponding {bind,unbind}* functions exposed
 * by this class.
 *
 * All config processing is done on the main thread, so instantiation of *ConfigProvider* objects
 * via createStaticConfigProvider() and createXdsConfigProvider() is naturally thread safe. Care
 * must be taken with regards to destruction of these objects, since it must also happen on the
 * main thread _prior_ to destruction of the ConfigProviderManagerImplBase object from which they
 * were created.
 *
 * This class can not be instantiated directly; instead, it provides the foundation for
 * dynamic config provider implementations which derive from it.
 */
class ConfigProviderManagerImplBase : public ConfigProviderManager, public Singleton::Instance {
public:
  /**
   * This is invoked by the /config_dump admin handler.
   * @return ProtobufTypes::MessagePtr the config dump proto corresponding to the associated
   *                                   config providers.
   */
  virtual ProtobufTypes::MessagePtr dumpConfigs() const PURE;

protected:
  // Ordered set for deterministic config dump output.
  using ConfigProviderSet = std::set<ConfigProvider*>;
  using ConfigProviderMap = std::unordered_map<ConfigProviderInstanceType,
                                               std::unique_ptr<ConfigProviderSet>, EnumClassHash>;
  using ConfigSubscriptionMap =
      std::unordered_map<uint64_t, std::weak_ptr<ConfigSubscriptionCommonBase>>;

  ConfigProviderManagerImplBase(Server::Admin& admin, const std::string& config_name);

  const ConfigSubscriptionMap& configSubscriptions() const { return config_subscriptions_; }

  /**
   * Returns the set of bound ImmutableConfigProviderBase-derived providers of a given type.
   * @param type supplies the type of config providers to return.
   * @return const ConfigProviderSet* the set of config providers corresponding to the type.
   */
  const ConfigProviderSet& immutableConfigProviders(ConfigProviderInstanceType type) const;

  /**
   * Returns the subscription associated with the config_source_proto; if none exists, a new one
   * is allocated according to the subscription_factory_fn.
   * @param config_source_proto supplies the proto specifying the config subscription parameters.
   * @param init_manager supplies the init manager.
   * @param subscription_factory_fn supplies a function to be called when a new subscription needs
   *                                to be allocated.
   * @return std::shared_ptr<T> an existing (if a match is found) or newly allocated subscription.
   */
  template <typename T>
  std::shared_ptr<T>
  getSubscription(const Protobuf::Message& config_source_proto, Init::Manager& init_manager,
                  const std::function<ConfigSubscriptionCommonBaseSharedPtr(
                      const uint64_t, ConfigProviderManagerImplBase&)>& subscription_factory_fn) {
    static_assert(std::is_base_of<ConfigSubscriptionCommonBase, T>::value,
                  "T must be a subclass of ConfigSubscriptionCommonBase");

    ConfigSubscriptionCommonBaseSharedPtr subscription;
    const uint64_t manager_identifier = MessageUtil::hash(config_source_proto);

    auto it = config_subscriptions_.find(manager_identifier);
    if (it == config_subscriptions_.end()) {
      // std::make_shared does not work for classes with private constructors. There are ways
      // around it. However, since this is not a performance critical path we err on the side
      // of simplicity.
      subscription = subscription_factory_fn(manager_identifier, *this);
      init_manager.add(subscription->init_target_);

      bindSubscription(manager_identifier, subscription);
    } else {
      // Because the ConfigProviderManagerImplBase's weak_ptrs only get cleaned up
      // in the ConfigSubscriptionCommonBase destructor, and the single threaded nature
      // of this code, locking the weak_ptr will not fail.
      subscription = it->second.lock();
    }
    ASSERT(subscription);

    return std::static_pointer_cast<T>(subscription);
  }

private:
  void bindSubscription(const uint64_t manager_identifier,
                        ConfigSubscriptionCommonBaseSharedPtr& subscription) {
    config_subscriptions_.insert({manager_identifier, subscription});
  }

  void unbindSubscription(const uint64_t manager_identifier) {
    config_subscriptions_.erase(manager_identifier);
  }

  void bindImmutableConfigProvider(ImmutableConfigProviderBase* provider);
  void unbindImmutableConfigProvider(ImmutableConfigProviderBase* provider);

  // TODO(jsedgwick) These two members are prime candidates for the owned-entry list/map
  // as in ConfigTracker. I.e. the ProviderImpls would have an EntryOwner for these lists
  // Then the lifetime management stuff is centralized and opaque.
  ConfigSubscriptionMap config_subscriptions_;
  ConfigProviderMap immutable_config_providers_map_;

  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;

  // See comment for friend classes in the ConfigSubscriptionCommonBase for more details on
  // the use of friends.
  friend class ConfigSubscriptionCommonBase;
  friend class ImmutableConfigProviderBase;
};

} // namespace Config
} // namespace Envoy
