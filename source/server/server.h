#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_manager_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/connection_handler_impl.h"
#include "server/http/admin.h"
#include "server/test_hooks.h"
#include "server/worker.h"

namespace Lyft {
namespace Server {

/**
 * All server wide stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SERVER_STATS(GAUGE)                                                                    \
  GAUGE(uptime)                                                                                    \
  GAUGE(memory_allocated)                                                                          \
  GAUGE(memory_heap_size)                                                                          \
  GAUGE(live)                                                                                      \
  GAUGE(parent_connections)                                                                        \
  GAUGE(total_connections)                                                                         \
  GAUGE(version)                                                                                   \
  GAUGE(days_until_first_cert_expiring)
// clang-format on

struct ServerStats {
  ALL_SERVER_STATS(GENERATE_GAUGE_STRUCT)
};

/**
 * Interface for creating service components during boot.
 */
class ComponentFactory {
public:
  virtual ~ComponentFactory() {}

  /**
   * @return DrainManagerPtr a new drain manager for the server.
   */
  virtual DrainManagerPtr createDrainManager(Instance& server) PURE;

  /**
   * @return Runtime::LoaderPtr the runtime implementation for the server.
   */
  virtual Runtime::LoaderPtr createRuntime(Instance& server, Configuration::Initial& config) PURE;
};

/**
 * Helpers used during server creation.
 */
class InstanceUtil : Logger::Loggable<Logger::Id::main> {
public:
  /**
   * Default implementation of runtime loader creation used in the real server and in most
   * integration tests where a mock runtime is not needed.
   */
  static Runtime::LoaderPtr createRuntime(Instance& server, Server::Configuration::Initial& config);
};

/**
 * Implementation of Init::Manager for use during post cluster manager init / pre listening.
 */
class InitManagerImpl : public Init::Manager {
public:
  void initialize(std::function<void()> callback);

  // Init::Manager
  void registerTarget(Init::Target& target) override;

private:
  enum class State { NotInitialized, Initializing, Initialized };

  std::list<Init::Target*> targets_;
  State state_{State::NotInitialized};
  std::function<void()> callback_;
};

/**
 * This is the actual full standalone server which stiches together various common components.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>, public Instance {
public:
  InstanceImpl(Options& options, TestHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
               Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
               const LocalInfo::LocalInfo& local_info);

  ~InstanceImpl() override;

  void run();

  // Server::Instance
  Admin& admin() override { return *admin_; }
  Api::Api& api() override { return handler_.api(); }
  Upstream::ClusterManager& clusterManager() override;
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return handler_.dispatcher(); }
  Network::DnsResolver& dnsResolver() override { return *dns_resolver_; }
  bool draining() override { return drain_manager_->draining(); }
  void drainListeners() override;
  DrainManager& drainManager() override { return *drain_manager_; }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  void failHealthcheck(bool fail) override;
  int getListenSocketFd(const std::string& address) override;
  Network::ListenSocket* getListenSocketByIndex(uint32_t index) override;
  void getParentStats(HotRestart::GetParentStatsInfo& info) override;
  HotRestart& hotRestart() override { return restarter_; }
  Init::Manager& initManager() override { return init_manager_; }
  Runtime::RandomGenerator& random() override { return random_generator_; }
  RateLimit::ClientPtr
  rateLimitClient(const Optional<std::chrono::milliseconds>& timeout) override {
    return config_->rateLimitClientFactory().create(timeout);
  }
  Runtime::Loader& runtime() override;
  void shutdown() override;
  void shutdownAdmin() override;
  bool healthCheckFailed() override;
  Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { return start_time_; }
  time_t startTimeFirstEpoch() override { return original_start_time_; }
  Stats::Store& stats() override { return stats_store_; }
  Tracing::HttpTracer& httpTracer() override;
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  const LocalInfo::LocalInfo& localInfo() override { return local_info_; }

private:
  void flushStats();
  void initialize(Options& options, TestHooks& hooks, ComponentFactory& component_factory);
  void initializeStatSinks();
  void loadServerFlags(const Optional<std::string>& flags_path);
  uint64_t numConnections();
  void startWorkers(TestHooks& hooks);

  Options& options_;
  HotRestart& restarter_;
  const time_t start_time_;
  time_t original_start_time_;
  Stats::StoreRoot& stats_store_;
  std::list<Stats::SinkPtr> stat_sinks_;
  ServerStats server_stats_;
  ThreadLocal::InstanceImpl thread_local_;
  SocketMap socket_map_;
  ConnectionHandlerImpl handler_;
  Runtime::RandomGeneratorImpl random_generator_;
  Runtime::LoaderPtr runtime_loader_;
  std::unique_ptr<Ssl::ContextManagerImpl> ssl_context_manager_;
  std::unique_ptr<Configuration::Main> config_;
  std::list<WorkerPtr> workers_;
  Stats::ScopePtr admin_scope_;
  std::unique_ptr<AdminImpl> admin_;
  Event::SignalEventPtr sigterm_;
  Event::SignalEventPtr sig_usr_1_;
  Event::SignalEventPtr sig_hup_;
  Network::DnsResolverPtr dns_resolver_;
  Event::TimerPtr stat_flush_timer_;
  const LocalInfo::LocalInfo& local_info_;
  DrainManagerPtr drain_manager_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  InitManagerImpl init_manager_;
  std::unique_ptr<Server::GuardDog> guard_dog_;
};

} // Server
} // Lyft