#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "envoy/api/api.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/init/manager.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Router {

class CommonConfigImpl;
using CommonConfigSharedPtr = std::shared_ptr<CommonConfigImpl>;
class VirtualHostImpl;
using VirtualHostImplConstSharedPtr = std::shared_ptr<const VirtualHostImpl>;

/**
 * Represents a virtual host slot in RouteMatcher, managing either an eagerly constructed
 * VirtualHostImpl or a dormant arena-owned VirtualHost proto inflated on first request match.
 *
 * Ownership is returned by VirtualHostImplConstSharedPtr and dormant protos remain live in
 * RouteMatcher's arena for the lifetime of the RouteMatcher so that future idle eviction can reset
 * active_vhost_ and re-inflate on demand without risking use-after-free on in-flight requests.
 *
 * This is a VirtualHost analogue of the ClusterInitializationObject used for deferred Cluster
 * initialization.
 */
class DomainEntry : Logger::Loggable<Logger::Id::router> {
public:
  DomainEntry() = default;
  ~DomainEntry();

  DomainEntry(const DomainEntry&) = delete;
  DomainEntry& operator=(const DomainEntry&) = delete;

  void initEager(VirtualHostImplConstSharedPtr vhost);
  void initDeferred(const envoy::config::route::v3::VirtualHost* vhost_proto);

  VirtualHostImplConstSharedPtr
  getOrCreateVirtualHost(const CommonConfigSharedPtr& global_route_config,
                         Server::Configuration::ServerFactoryContext& factory_context,
                         const Stats::ScopeSharedPtr& vhost_scope) const {
    if (deferred_vhost_proto_ == nullptr || is_inflated_.load(std::memory_order_acquire)) {
      return active_vhost_;
    }
    return inflateVirtualHost(global_route_config, factory_context, vhost_scope);
  }

  VirtualHostImplConstSharedPtr activeVirtualHost() const {
    if (deferred_vhost_proto_ == nullptr || is_inflated_.load(std::memory_order_acquire)) {
      return active_vhost_;
    }
    return nullptr;
  }

  bool isDeferred() const { return deferred_vhost_proto_ != nullptr; }

private:
  VirtualHostImplConstSharedPtr
  inflateVirtualHost(const CommonConfigSharedPtr& global_route_config,
                     Server::Configuration::ServerFactoryContext& factory_context,
                     const Stats::ScopeSharedPtr& vhost_scope) const;

  const envoy::config::route::v3::VirtualHost* deferred_vhost_proto_{nullptr};
  mutable absl::Mutex init_mutex_;
  mutable std::atomic<bool> is_inflated_{false};
  mutable VirtualHostImplConstSharedPtr active_vhost_;
};

/**
 * Base ServerFactoryContext decorator that forwards all virtual methods to delegate()
 * (parent_context_ by default), allowing validation, tracking, and worker-safe context wrappers to
 * override only the methods they customize.
 */
class DelegatingServerFactoryContext : public Server::Configuration::ServerFactoryContext {
public:
  explicit DelegatingServerFactoryContext(
      Server::Configuration::ServerFactoryContext& parent_context)
      : parent_context_(parent_context) {}

  // Server::Configuration::ServerFactoryContext
  Stats::Scope& scope() override { return delegate().scope(); }
  Stats::Scope& serverScope() override { return delegate().serverScope(); }
  Init::Manager& initManager() override { return delegate().initManager(); }
  Singleton::Manager& singletonManager() override { return delegate().singletonManager(); }
  ThreadLocal::Instance& threadLocal() override { return delegate().threadLocal(); }
  Secret::SecretManager& secretManager() override { return delegate().secretManager(); }
  Upstream::ClusterManager& clusterManager() override { return delegate().clusterManager(); }
  Envoy::Config::XdsManager& xdsManager() override { return delegate().xdsManager(); }
  Http::HttpServerPropertiesCacheManager& httpServerPropertiesCacheManager() override {
    return delegate().httpServerPropertiesCacheManager();
  }
  Event::Dispatcher& mainThreadDispatcher() override { return delegate().mainThreadDispatcher(); }
  const Server::Options& options() override { return delegate().options(); }
  const LocalInfo::LocalInfo& localInfo() const override { return delegate().localInfo(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return delegate().messageValidationContext();
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return delegate().messageValidationVisitor();
  }
  Envoy::Runtime::Loader& runtime() override { return delegate().runtime(); }
  OptRef<Server::Admin> admin() override { return delegate().admin(); }
  TimeSource& timeSource() override { return delegate().timeSource(); }
  AccessLog::AccessLogManager& accessLogManager() override { return delegate().accessLogManager(); }
  Api::Api& api() override { return delegate().api(); }
  Http::Context& httpContext() override { return delegate().httpContext(); }
  Grpc::Context& grpcContext() override { return delegate().grpcContext(); }
  Router::Context& routerContext() override { return delegate().routerContext(); }
  ProcessContextOptRef processContext() override { return delegate().processContext(); }
  Envoy::Server::DrainManager& drainManager() override { return delegate().drainManager(); }
  Server::ServerLifecycleNotifier& lifecycleNotifier() override {
    return delegate().lifecycleNotifier();
  }
  Regex::Engine& regexEngine() override { return delegate().regexEngine(); }
  Server::Configuration::StatsConfig& statsConfig() override { return delegate().statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return delegate().bootstrap(); }
  Server::OverloadManager& overloadManager() override { return delegate().overloadManager(); }
  Server::OverloadManager& nullOverloadManager() override {
    return delegate().nullOverloadManager();
  }
  bool healthCheckFailed() const override { return delegate().healthCheckFailed(); }
  Ssl::ContextManager& sslContextManager() override { return delegate().sslContextManager(); }

protected:
  virtual Server::Configuration::ServerFactoryContext& delegate() const { return parent_context_; }

  Server::Configuration::ServerFactoryContext& parent_context_;
};

/**
 * Isolated validation context used to probe-validate virtual hosts and their route structures on
 * the main thread at configuration ingestion time without polluting server stats/singletons/TLS
 * slots or registering orphaned targets on the RDS warming Init::Manager.
 *
 * Also tracks whether any extension accessed main-thread-bound resources (Init::Manager targets,
 * Singleton::Manager, ThreadLocal::Instance, Secret::SecretManager, ClusterManager, XdsManager,
 * mainThreadDispatcher, Admin, AccessLogManager, DrainManager, ServerLifecycleNotifier,
 * SslContextManager, Api::allocateDispatcher, Api::customStatNamespaces, or
 * ServerFactoryContextInstance) so RouteMatcher can fall back to eager main-thread initialization.
 */
class IsolatedValidationContext : public DelegatingServerFactoryContext {
public:
  explicit IsolatedValidationContext(Server::Configuration::ServerFactoryContext& parent_context);

  bool requiresMainThreadInit() const {
    return isolated_init_manager_.hasTargets() || used_main_thread_resource_;
  }
  void markMainThreadResourceUsed() { used_main_thread_resource_ = true; }

  // Server::Configuration::ServerFactoryContext
  Stats::Scope& scope() override { return *isolated_scope_; }
  Stats::Scope& serverScope() override { return *isolated_scope_; }
  Init::Manager& initManager() override { return isolated_init_manager_; }
  Singleton::Manager& singletonManager() override { return tracking_singleton_manager_; }
  ThreadLocal::Instance& threadLocal() override { return tracking_thread_local_; }
  Api::Api& api() override { return tracking_api_; }

  Secret::SecretManager& secretManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.secretManager();
  }
  Upstream::ClusterManager& clusterManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.clusterManager();
  }
  Envoy::Config::XdsManager& xdsManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.xdsManager();
  }
  Event::Dispatcher& mainThreadDispatcher() override {
    used_main_thread_resource_ = true;
    return parent_context_.mainThreadDispatcher();
  }
  OptRef<Server::Admin> admin() override {
    used_main_thread_resource_ = true;
    return parent_context_.admin();
  }
  AccessLog::AccessLogManager& accessLogManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.accessLogManager();
  }
  Envoy::Server::DrainManager& drainManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.drainManager();
  }
  Server::ServerLifecycleNotifier& lifecycleNotifier() override {
    used_main_thread_resource_ = true;
    return parent_context_.lifecycleNotifier();
  }
  Ssl::ContextManager& sslContextManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.sslContextManager();
  }

private:
  class TrackingInitManager : public Init::Manager {
  public:
    State state() const override { return State::Uninitialized; }
    void add(const Init::Target&) override { target_registered_ = true; }
    void initialize(const Init::Watcher& watcher) override {
      watcher.createHandle("isolated_validation")->ready();
    }
    void updateWatcher(const Init::Watcher&) override {}
    void dumpUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps&) override {}

    bool hasTargets() const { return target_registered_; }

  private:
    bool target_registered_{false};
  };

  class TrackingSingletonManager : public Singleton::Manager {
  public:
    TrackingSingletonManager(Server::Configuration::ServerFactoryContext& parent_context,
                             bool& used_main_thread_resource)
        : parent_context_(parent_context), used_main_thread_resource_(used_main_thread_resource) {}

    Singleton::InstanceSharedPtr get(const std::string& name, Singleton::SingletonFactoryCb cb,
                                     bool pin) override;

  private:
    Server::Configuration::ServerFactoryContext& parent_context_;
    Singleton::ManagerImpl local_manager_;
    bool& used_main_thread_resource_;
  };

  class TrackingThreadLocalInstance : public ThreadLocal::Instance {
  public:
    TrackingThreadLocalInstance(Server::Configuration::ServerFactoryContext& parent_context,
                                bool& used_main_thread_resource)
        : parent_context_(parent_context), used_main_thread_resource_(used_main_thread_resource) {}

    ThreadLocal::SlotSharedPtr allocateSlot() override;
    void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override {
      parent_context_.threadLocal().registerThread(dispatcher, main_thread);
    }
    void shutdownGlobalThreading() override {
      parent_context_.threadLocal().shutdownGlobalThreading();
    }
    void shutdownThread() override { parent_context_.threadLocal().shutdownThread(); }
    Event::Dispatcher& dispatcher() override { return parent_context_.threadLocal().dispatcher(); }
    bool isShutdown() const override { return parent_context_.threadLocal().isShutdown(); }

  private:
    Server::Configuration::ServerFactoryContext& parent_context_;
    bool& used_main_thread_resource_;
  };

  class TrackingApi : public Api::Api {
  public:
    TrackingApi(Server::Configuration::ServerFactoryContext& parent_context,
                Stats::Scope& isolated_scope, bool& used_main_thread_resource)
        : parent_context_(parent_context), isolated_scope_(isolated_scope),
          used_main_thread_resource_(used_main_thread_resource) {}

    Event::DispatcherPtr allocateDispatcher(const std::string& name) override {
      used_main_thread_resource_ = true;
      return parent_context_.api().allocateDispatcher(name);
    }
    Event::DispatcherPtr
    allocateDispatcher(const std::string& name,
                       const Event::ScaledRangeTimerManagerFactory& scaled_timer_factory) override {
      used_main_thread_resource_ = true;
      return parent_context_.api().allocateDispatcher(name, scaled_timer_factory);
    }
    Event::DispatcherPtr
    allocateDispatcher(const std::string& name,
                       Buffer::WatermarkFactoryPtr&& watermark_factory) override {
      used_main_thread_resource_ = true;
      return parent_context_.api().allocateDispatcher(name, std::move(watermark_factory));
    }
    Thread::ThreadFactory& threadFactory() override {
      return parent_context_.api().threadFactory();
    }
    Filesystem::Instance& fileSystem() override { return parent_context_.api().fileSystem(); }
    TimeSource& timeSource() override { return parent_context_.api().timeSource(); }
    Stats::Scope& rootScope() override { return isolated_scope_; }
    Random::RandomGenerator& randomGenerator() override {
      return parent_context_.api().randomGenerator();
    }
    ProcessContextOptRef processContext() override {
      return parent_context_.api().processContext();
    }
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap() const override {
      return parent_context_.api().bootstrap();
    }
    Stats::CustomStatNamespaces& customStatNamespaces() override {
      used_main_thread_resource_ = true;
      return parent_context_.api().customStatNamespaces();
    }

  private:
    Server::Configuration::ServerFactoryContext& parent_context_;
    Stats::Scope& isolated_scope_;
    bool& used_main_thread_resource_;
  };

  Stats::IsolatedStoreImpl isolated_store_;
  Stats::ScopeSharedPtr isolated_scope_;
  TrackingInitManager isolated_init_manager_;
  bool used_main_thread_resource_{false};
  TrackingSingletonManager tracking_singleton_manager_;
  TrackingThreadLocalInstance tracking_thread_local_;
  TrackingApi tracking_api_;
};

/**
 * RAII guard that routes main-thread ServerFactoryContextInstance::get() / getExisting() lookups
 * during probe validation through a tracking proxy backed by IsolatedValidationContext, recording
 * any singleton server-context access (which would fail on worker threads) and restoring the
 * previous ServerFactoryContextInstance on scope exit.
 */
class ScopedValidationServerContext {
public:
  explicit ScopedValidationServerContext(IsolatedValidationContext& validation_context);
  ~ScopedValidationServerContext();

  ScopedValidationServerContext(const ScopedValidationServerContext&) = delete;
  ScopedValidationServerContext& operator=(const ScopedValidationServerContext&) = delete;

private:
  class TrackingServerContextProxy : public DelegatingServerFactoryContext {
  public:
    explicit TrackingServerContextProxy(IsolatedValidationContext& target)
        : DelegatingServerFactoryContext(target), target_(target) {}

  protected:
    Server::Configuration::ServerFactoryContext& delegate() const override {
      target_.markMainThreadResourceUsed();
      return target_;
    }

  private:
    IsolatedValidationContext& target_;
  };

  TrackingServerContextProxy proxy_;
  Server::Configuration::ServerFactoryContext* prev_;
};

/**
 * Recursive Protobuf::Reflection check returning true if the virtual host contains features or
 * extension configurations that require (or are not vetted against) main-thread initialization,
 * allowing RouteMatcher to skip throwaway probe validation and initialize eagerly once.
 */
bool requiresMainThreadInit(const envoy::config::route::v3::VirtualHost& vhost_proto);

/**
 * Returns true if the virtual host configuration requires full isolated probe validation at
 * ingestion time, or false if it only uses simple fast-path fields validated directly here.
 */
bool requiresProbeValidation(const envoy::config::route::v3::VirtualHost& vhost_proto,
                             const CommonConfigSharedPtr& global_route_config,
                             bool validate_clusters,
                             Server::Configuration::ServerFactoryContext& factory_context);

} // namespace Router
} // namespace Envoy
