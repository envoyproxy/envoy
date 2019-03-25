#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/instance.h"
#include "envoy/server/tracer_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_manager_impl.h"
#include "common/common/assert.h"
#include "common/common/logger_delegates.h"
#include "common/grpc/async_client_manager_impl.h"
#include "common/http/context_impl.h"
#include "common/memory/heap_shrinker.h"
#include "common/runtime/runtime_impl.h"
#include "common/secret/secret_manager_impl.h"
#include "common/upstream/health_discovery_service.h"

#include "server/configuration_impl.h"
#include "server/http/admin.h"
#include "server/init_manager_impl.h"
#include "server/listener_manager_impl.h"
#include "server/overload_manager_impl.h"
#include "server/test_hooks.h"
#include "server/worker_impl.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

/**
 * All server wide stats. @see stats_macros.h
 */
// clang-format off
#define ALL_SERVER_STATS(COUNTER, GAUGE)                                                           \
  GAUGE(uptime)                                                                                    \
  GAUGE(concurrency)                                                                               \
  GAUGE(memory_allocated)                                                                          \
  GAUGE(memory_heap_size)                                                                          \
  GAUGE(live)                                                                                      \
  GAUGE(parent_connections)                                                                        \
  GAUGE(total_connections)                                                                         \
  GAUGE(version)                                                                                   \
  GAUGE(days_until_first_cert_expiring)                                                            \
  GAUGE(hot_restart_epoch)                                                                         \
  COUNTER(debug_assertion_failures)
// clang-format on

struct ServerStats {
  ALL_SERVER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
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
  enum class BootstrapVersion { V2 };

  /**
   * Default implementation of runtime loader creation used in the real server and in most
   * integration tests where a mock runtime is not needed.
   */
  static Runtime::LoaderPtr createRuntime(Instance& server, Server::Configuration::Initial& config);

  /**
   * Helper for flushing counters, gauges and histograms to sinks. This takes care of calling
   * flush() on each sink and clearing the cache afterward.
   * @param sinks supplies the list of sinks.
   * @param source provides the metrics being flushed.
   */
  static void flushMetricsToSinks(const std::list<Stats::SinkPtr>& sinks, Stats::Source& source);

  /**
   * Load a bootstrap config from either v1 or v2 and perform validation.
   * @param bootstrap supplies the bootstrap to fill.
   * @param config_path supplies the config path.
   * @param v2_only supplies whether to attempt v1 fallback.
   * @param api reference to the Api object
   * @return BootstrapVersion to indicate which version of the API was parsed.
   */
  static BootstrapVersion loadBootstrapConfig(envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                              const Options& options, Api::Api& api);
};

/**
 * This is a helper used by InstanceImpl::run() on the stack. It's broken out to make testing
 * easier.
 */
class RunHelper : Logger::Loggable<Logger::Id::main> {
public:
  RunHelper(Instance& instance, const Options& options, Event::Dispatcher& dispatcher,
            Upstream::ClusterManager& cm, AccessLog::AccessLogManager& access_log_manager,
            InitManagerImpl& init_manager, OverloadManager& overload_manager,
            std::function<void()> workers_start_cb);

private:
  Event::SignalEventPtr sigterm_;
  Event::SignalEventPtr sigint_;
  Event::SignalEventPtr sig_usr_1_;
  Event::SignalEventPtr sig_hup_;
};

/**
 * This is the actual full standalone server which stitches together various common components.
 */
class InstanceImpl : Logger::Loggable<Logger::Id::main>,
                     public Instance,
                     public ServerLifecycleNotifier {
public:
  /**
   * @throw EnvoyException if initialization fails.
   */
  InstanceImpl(const Options& options, Event::TimeSystem& time_system,
               Network::Address::InstanceConstSharedPtr local_address, TestHooks& hooks,
               HotRestart& restarter, Stats::StoreRoot& store,
               Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
               Runtime::RandomGeneratorPtr&& random_generator, ThreadLocal::Instance& tls,
               Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system);

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
  ServerLifecycleNotifier& lifecycleNotifier() override { return *this; }
  ListenerManager& listenerManager() override { return *listener_manager_; }
  Secret::SecretManager& secretManager() override { return *secret_manager_; }
  Envoy::MutexTracer* mutexTracer() override { return mutex_tracer_; }
  OverloadManager& overloadManager() override { return *overload_manager_; }
  Runtime::RandomGenerator& random() override { return *random_generator_; }
  Runtime::Loader& runtime() override;
  void shutdown() override;
  bool isShutdown() override final { return shutdown_; }
  void shutdownAdmin() override;
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  bool healthCheckFailed() override;
  const Options& options() override { return options_; }
  time_t startTimeCurrentEpoch() override { return start_time_; }
  time_t startTimeFirstEpoch() override { return original_start_time_; }
  Stats::Store& stats() override { return stats_store_; }
  Http::Context& httpContext() override { return http_context_; }
  ThreadLocal::Instance& threadLocal() override { return thread_local_; }
  const LocalInfo::LocalInfo& localInfo() override { return *local_info_; }
  TimeSource& timeSource() override { return time_source_; }

  std::chrono::milliseconds statsFlushInterval() const override {
    return config_.statsFlushInterval();
  }

  // ServerLifecycleNotifier
  void registerCallback(Stage stage, StageCallback callback) override;
  void registerCallback(Stage stage, StageCallbackWithCompletion callback) override;

private:
  ProtobufTypes::MessagePtr dumpBootstrapConfig();
  void flushStats();
  void initialize(const Options& options, Network::Address::InstanceConstSharedPtr local_address,
                  ComponentFactory& component_factory, TestHooks& hooks);
  void loadServerFlags(const absl::optional<std::string>& flags_path);
  uint64_t numConnections();
  void startWorkers();
  void terminate();
  void notifyCallbacksForStage(Stage stage, Event::PostCb completion_cb = [] {});

  // init_manager_ must come before any member that participates in initialization, and destructed
  // only after referencing members are gone, since initialization continuation can potentially
  // occur at any point during member lifetime.
  InitManagerImpl init_manager_{"Server"};
  // secret_manager_ must come before listener_manager_, config_ and dispatcher_, and destructed
  // only after these members can no longer reference it, since:
  // - There may be active filter chains referencing it in listener_manager_.
  // - There may be active clusters referencing it in config_.cluster_manager_.
  // - There may be active connections referencing it.
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  bool shutdown_;
  const Options& options_;
  TimeSource& time_source_;
  HotRestart& restarter_;
  const time_t start_time_;
  time_t original_start_time_;
  Stats::StoreRoot& stats_store_;
  std::unique_ptr<ServerStats> server_stats_;
  Assert::ActionRegistrationPtr assert_action_registration_;
  ThreadLocal::Instance& thread_local_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<AdminImpl> admin_;
  Singleton::ManagerPtr singleton_manager_;
  Network::ConnectionHandlerPtr handler_;
  Runtime::RandomGeneratorPtr random_generator_;
  std::unique_ptr<Runtime::ScopedLoaderSingleton> runtime_singleton_;
  std::unique_ptr<Extensions::TransportSockets::Tls::ContextManagerImpl> ssl_context_manager_;
  ProdListenerComponentFactory listener_component_factory_;
  ProdWorkerFactory worker_factory_;
  std::unique_ptr<ListenerManager> listener_manager_;
  Configuration::MainImpl config_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Event::TimerPtr stat_flush_timer_;
  LocalInfo::LocalInfoPtr local_info_;
  DrainManagerPtr drain_manager_;
  AccessLog::AccessLogManagerImpl access_log_manager_;
  std::unique_ptr<Upstream::ClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<Server::GuardDog> guard_dog_;
  bool terminated_;
  std::unique_ptr<Logger::FileSinkDelegate> file_logger_;
  envoy::config::bootstrap::v2::Bootstrap bootstrap_;
  ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  SystemTime bootstrap_config_update_time_;
  Grpc::AsyncClientManagerPtr async_client_manager_;
  Upstream::ProdClusterInfoFactory info_factory_;
  Upstream::HdsDelegatePtr hds_delegate_;
  std::unique_ptr<OverloadManagerImpl> overload_manager_;
  std::unique_ptr<RunHelper> run_helper_;
  Envoy::MutexTracer* mutex_tracer_;
  Http::ContextImpl http_context_;
  std::unique_ptr<Memory::HeapShrinker> heap_shrinker_;
  const std::thread::id main_thread_id_;
  std::unordered_map<Stage, std::vector<StageCallback>> stage_callbacks_;
  std::unordered_map<Stage, std::vector<StageCallbackWithCompletion>> stage_completable_callbacks_;
};

} // namespace Server
} // namespace Envoy
