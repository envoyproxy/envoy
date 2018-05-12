#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/server/configuration.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_manager_impl.h"
#include "common/common/logger_delegates.h"
#include "common/runtime/runtime_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "server/http/admin.h"
#include "server/init_manager_impl.h"
#include "server/listener_manager_impl.h"
#include "server/test_hooks.h"
#include "server/worker_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
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
  enum class BootstrapVersion { V1, V2 };

  /**
   * Default implementation of runtime loader creation used in the real server and in most
   * integration tests where a mock runtime is not needed.
   */
  static Runtime::LoaderPtr createRuntime(Instance& server, Server::Configuration::Initial& config);

  /**
   * Helper for flushing counters, gauges and hisograms to sinks. This takes care of calling
   * beginFlush(), latching of counters and flushing, flushing of gauges, and calling endFlush(), on
   * each sink.
   * @param sinks supplies the list of sinks.
   * @param store supplies the store to flush.
   */
  static void flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks, Stats::Store& store);

  /**
   * Load a bootstrap config from either v1 or v2 and perform validation.
   * @param bootstrap supplies the bootstrap to fill.
   * @param config_path supplies the config path.
   * @param v2_only supplies whether to attempt v1 fallback.
   * @return BootstrapVersion to indicate which version of the API was parsed.
   */
  static BootstrapVersion loadBootstrapConfig(envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                              Options& options);
};

/**
 * This is a helper used by InstanceImpl::run() on the stack. It's broken out to make testing
 * easier.
 */
class RunHelper : Logger::Loggable<Logger::Id::main> {
public:
  RunHelper(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, HotRestart& hot_restart,
            AccessLog::AccessLogManager& access_log_manager, InitManagerImpl& init_manager,
            std::function<void()> workers_start_cb);

private:
  Event::SignalEventPtr sigterm_;
  Event::SignalEventPtr sig_usr_1_;
  Event::SignalEventPtr sig_hup_;
  bool shutdown_{};
};

/**
 * This is the actual full standalone server which stiches together various common components.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>, public Instance {
public:
  /**
   * @throw EnvoyException if initialization fails.
   */
  InstanceImpl(Options& options, Network::Address::InstanceConstSharedPtr local_address,
               TestHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
               Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
               Runtime::RandomGeneratorPtr&& random_generator, ThreadLocal::Instance& tls);

  ~InstanceImpl() override;

  void run();

  // Server::Instance
  Admin& admin() override { return *admin_; }
  Api::Api& api() override { return *api_; }
  Upstream::ClusterManager& clusterManager() override;
  Ssl::ContextManager& sslContextManager() override { return *ssl_context_manager_; }
  Event::Dispatcher& dispatcher() override { return *dispatcher_; }
  Network::DnsResolverSharedPtr dnsResolver() override { return dns_resolver_; }
  void drainListeners() override;
  DrainManager& drainManager() override { return *drain_manager_; }
  AccessLog::AccessLogManager& accessLogManager() override { return access_log_manager_; }
  void failHealthcheck(bool fail) override;
  void getParentStats(HotRestart::GetParentStatsInfo& info) override;
  HotRestart& hotRestart() override { return restarter_; }
  Init::Manager& initManager() override { return init_manager_; }
  ListenerManager& listenerManager() override { return *listener_manager_; }
  Secret::SecretManager& secretManager() override { return *secret_manager_; }
  Runtime::RandomGenerator& random() override { return *random_generator_; }
  RateLimit::ClientPtr
  rateLimitClient(const absl::optional<std::chrono::milliseconds>& timeout) override {
    return config_->rateLimitClientFactory().create(timeout);
  }
  Runtime::Loader& runtime() override;
  void shutdown() override;
  void shutdownAdmin() override;
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  bool healthCheckFailed() override;
  Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { return start_time_; }
  time_t startTimeFirstEpoch() override { return original_start_time_; }
  Stats::Store& stats() override { return stats_store_; }
  Tracing::HttpTracer& httpTracer() override;
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  const LocalInfo::LocalInfo& localInfo() override { return *local_info_; }

private:
  void flushStats();
  void initialize(Options& options, Network::Address::InstanceConstSharedPtr local_address,
                  ComponentFactory& component_factory);
  void loadServerFlags(const absl::optional<std::string>& flags_path);
  uint64_t numConnections();
  void startWorkers();
  void terminate();

  Options& options_;
  HotRestart& restarter_;
  const time_t start_time_;
  time_t original_start_time_;
  Stats::StoreRoot& stats_store_;
  std::unique_ptr<ServerStats> server_stats_;
  ThreadLocal::Instance& thread_local_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<AdminImpl> admin_;
  Singleton::ManagerPtr singleton_manager_;
  Network::ConnectionHandlerPtr handler_;
  Runtime::RandomGeneratorPtr random_generator_;
  Runtime::LoaderPtr runtime_loader_;
  std::unique_ptr<Ssl::ContextManagerImpl> ssl_context_manager_;
  ProdListenerComponentFactory listener_component_factory_;
  ProdWorkerFactory worker_factory_;
  std::unique_ptr<ListenerManager> listener_manager_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  std::unique_ptr<Configuration::Main> config_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Event::TimerPtr stat_flush_timer_;
  LocalInfo::LocalInfoPtr local_info_;
  DrainManagerPtr drain_manager_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::unique_ptr<Upstream::ClusterManagerFactory> cluster_manager_factory_;
  InitManagerImpl init_manager_;
  std::unique_ptr<Server::GuardDog> guard_dog_;
  bool terminated_;
  std::unique_ptr<Logger::FileSinkDelegate> file_logger_;
};

} // namespace Server
} // namespace Envoy
