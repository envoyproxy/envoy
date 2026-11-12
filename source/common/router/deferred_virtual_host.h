#pragma once

#include <atomic>
#include <memory>
#include <string>

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
 * Isolated validation context used to probe-validate virtual hosts and their route structures on
 * the main thread at configuration ingestion time without polluting server stats/singletons/TLS
 * slots or registering orphaned targets on the RDS warming Init::Manager.
 *
 * Also tracks whether any extension accessed main-thread-bound resources (Init::Manager targets,
 * Singleton::Manager, ThreadLocal::Instance, Secret::SecretManager, or
 * ServerFactoryContextInstance) so RouteMatcher can fall back to eager main-thread initialization.
 */
class IsolatedValidationContext : public Server::Configuration::ServerFactoryContext {
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
  Secret::SecretManager& secretManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.secretManager();
  }

  Upstream::ClusterManager& clusterManager() override { return parent_context_.clusterManager(); }
  Envoy::Config::XdsManager& xdsManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.xdsManager();
  }
  Http::HttpServerPropertiesCacheManager& httpServerPropertiesCacheManager() override {
    return parent_context_.httpServerPropertiesCacheManager();
  }
  Event::Dispatcher& mainThreadDispatcher() override {
    return parent_context_.mainThreadDispatcher();
  }
  const Server::Options& options() override { return parent_context_.options(); }
  const LocalInfo::LocalInfo& localInfo() const override { return parent_context_.localInfo(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return parent_context_.messageValidationContext();
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return parent_context_.messageValidationVisitor();
  }
  Envoy::Runtime::Loader& runtime() override { return parent_context_.runtime(); }
  OptRef<Server::Admin> admin() override {
    used_main_thread_resource_ = true;
    return parent_context_.admin();
  }
  TimeSource& timeSource() override { return parent_context_.timeSource(); }
  AccessLog::AccessLogManager& accessLogManager() override {
    return parent_context_.accessLogManager();
  }
  Api::Api& api() override { return parent_context_.api(); }
  Http::Context& httpContext() override { return parent_context_.httpContext(); }
  Grpc::Context& grpcContext() override { return parent_context_.grpcContext(); }
  Router::Context& routerContext() override { return parent_context_.routerContext(); }
  ProcessContextOptRef processContext() override { return parent_context_.processContext(); }
  Envoy::Server::DrainManager& drainManager() override {
    used_main_thread_resource_ = true;
    return parent_context_.drainManager();
  }
  Server::ServerLifecycleNotifier& lifecycleNotifier() override {
    used_main_thread_resource_ = true;
    return parent_context_.lifecycleNotifier();
  }
  Regex::Engine& regexEngine() override { return parent_context_.regexEngine(); }
  Server::Configuration::StatsConfig& statsConfig() override {
    return parent_context_.statsConfig();
  }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override {
    return parent_context_.bootstrap();
  }
  Server::OverloadManager& overloadManager() override { return parent_context_.overloadManager(); }
  Server::OverloadManager& nullOverloadManager() override {
    return parent_context_.nullOverloadManager();
  }
  bool healthCheckFailed() const override { return parent_context_.healthCheckFailed(); }
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
    TrackingSingletonManager(Singleton::Manager& parent, bool& used_main_thread_resource)
        : parent_(parent), used_main_thread_resource_(used_main_thread_resource) {}

    Singleton::InstanceSharedPtr get(const std::string& name, Singleton::SingletonFactoryCb cb,
                                     bool pin) override;

  private:
    Singleton::Manager& parent_;
    Singleton::ManagerImpl local_manager_;
    bool& used_main_thread_resource_;
  };

  class TrackingThreadLocalInstance : public ThreadLocal::Instance {
  public:
    TrackingThreadLocalInstance(ThreadLocal::Instance& parent, bool& used_main_thread_resource)
        : parent_(parent), used_main_thread_resource_(used_main_thread_resource) {}

    ThreadLocal::SlotSharedPtr allocateSlot() override;
    void registerThread(Event::Dispatcher& dispatcher, bool main_thread) override {
      parent_.registerThread(dispatcher, main_thread);
    }
    void shutdownGlobalThreading() override { parent_.shutdownGlobalThreading(); }
    void shutdownThread() override { parent_.shutdownThread(); }
    Event::Dispatcher& dispatcher() override { return parent_.dispatcher(); }
    bool isShutdown() const override { return parent_.isShutdown(); }

  private:
    ThreadLocal::Instance& parent_;
    bool& used_main_thread_resource_;
  };

  Server::Configuration::ServerFactoryContext& parent_context_;
  Stats::IsolatedStoreImpl isolated_store_;
  Stats::ScopeSharedPtr isolated_scope_;
  TrackingInitManager isolated_init_manager_;
  bool used_main_thread_resource_{false};
  TrackingSingletonManager tracking_singleton_manager_;
  TrackingThreadLocalInstance tracking_thread_local_;
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
  class TrackingServerContextProxy : public Server::Configuration::ServerFactoryContext {
  public:
    explicit TrackingServerContextProxy(IsolatedValidationContext& target) : target_(target) {}

    Stats::Scope& scope() override { return tracked().scope(); }
    Stats::Scope& serverScope() override { return tracked().serverScope(); }
    Init::Manager& initManager() override { return tracked().initManager(); }
    Singleton::Manager& singletonManager() override { return tracked().singletonManager(); }
    ThreadLocal::Instance& threadLocal() override { return tracked().threadLocal(); }
    Secret::SecretManager& secretManager() override { return tracked().secretManager(); }
    Upstream::ClusterManager& clusterManager() override { return tracked().clusterManager(); }
    Envoy::Config::XdsManager& xdsManager() override { return tracked().xdsManager(); }
    Http::HttpServerPropertiesCacheManager& httpServerPropertiesCacheManager() override {
      return tracked().httpServerPropertiesCacheManager();
    }
    Event::Dispatcher& mainThreadDispatcher() override { return tracked().mainThreadDispatcher(); }
    const Server::Options& options() override { return tracked().options(); }
    const LocalInfo::LocalInfo& localInfo() const override { return tracked().localInfo(); }
    ProtobufMessage::ValidationContext& messageValidationContext() override {
      return tracked().messageValidationContext();
    }
    ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
      return tracked().messageValidationVisitor();
    }
    Envoy::Runtime::Loader& runtime() override { return tracked().runtime(); }
    OptRef<Server::Admin> admin() override { return tracked().admin(); }
    TimeSource& timeSource() override { return tracked().timeSource(); }
    AccessLog::AccessLogManager& accessLogManager() override {
      return tracked().accessLogManager();
    }
    Api::Api& api() override { return tracked().api(); }
    Http::Context& httpContext() override { return tracked().httpContext(); }
    Grpc::Context& grpcContext() override { return tracked().grpcContext(); }
    Router::Context& routerContext() override { return tracked().routerContext(); }
    ProcessContextOptRef processContext() override { return tracked().processContext(); }
    Envoy::Server::DrainManager& drainManager() override { return tracked().drainManager(); }
    Server::ServerLifecycleNotifier& lifecycleNotifier() override {
      return tracked().lifecycleNotifier();
    }
    Regex::Engine& regexEngine() override { return tracked().regexEngine(); }
    Server::Configuration::StatsConfig& statsConfig() override { return tracked().statsConfig(); }
    envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return tracked().bootstrap(); }
    Server::OverloadManager& overloadManager() override { return tracked().overloadManager(); }
    Server::OverloadManager& nullOverloadManager() override {
      return tracked().nullOverloadManager();
    }
    bool healthCheckFailed() const override { return tracked().healthCheckFailed(); }
    Ssl::ContextManager& sslContextManager() override { return tracked().sslContextManager(); }

  private:
    IsolatedValidationContext& tracked() const {
      target_.markMainThreadResourceUsed();
      return target_;
    }

    IsolatedValidationContext& target_;
  };

  TrackingServerContextProxy proxy_;
  Server::Configuration::ServerFactoryContext* prev_;
};

/**
 * Shallow protobuf check returning true if the virtual host contains features that inherently
 * require main-thread initialization (route_specifiers, file-watched direct_response.body, custom
 * match tree actions, or dynamic_modules extensions across per-filter configs, matchers, and
 * formatters), allowing RouteMatcher to skip probe validation and initialize eagerly once.
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

/**
 * Represents a virtual host slot in RouteMatcher, managing either an eagerly constructed
 * VirtualHostImpl or a dormant arena-owned VirtualHost proto inflated on first request match.
 *
 * Ownership is returned by VirtualHostImplConstSharedPtr and dormant protos remain live in
 * RouteMatcher's arena for the lifetime of the RouteMatcher so that future idle eviction can reset
 * active_vhost_ and re-inflate on demand without risking use-after-free on in-flight requests.
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

} // namespace Router
} // namespace Envoy
