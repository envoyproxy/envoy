#include "server/server.h"

#include <signal.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "envoy/config/bootstrap/v2//bootstrap.pb.validate.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/api/api_impl.h"
#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/config/bootstrap_json.h"
#include "common/config/utility.h"
#include "common/local_info/local_info_impl.h"
#include "common/memory/stats.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"
#include "common/router/rds_impl.h"
#include "common/runtime/runtime_impl.h"
#include "common/singleton/manager_impl.h"
#include "common/stats/thread_local_store.h"
#include "common/upstream/cluster_manager_impl.h"

#include "server/configuration_impl.h"
#include "server/connection_handler_impl.h"
#include "server/guarddog_impl.h"
#include "server/test_hooks.h"

namespace Envoy {
namespace Server {

InstanceImpl::InstanceImpl(Options& options, Network::Address::InstanceConstSharedPtr local_address,
                           TestHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
                           Thread::BasicLockable& access_log_lock,
                           ComponentFactory& component_factory, ThreadLocal::Instance& tls)
    : options_(options), restarter_(restarter), start_time_(time(nullptr)),
      original_start_time_(start_time_), stats_store_(store), thread_local_(tls),
      api_(new Api::Impl(options.fileFlushIntervalMsec())), dispatcher_(api_->allocateDispatcher()),
      singleton_manager_(new Singleton::ManagerImpl()),
      handler_(new ConnectionHandlerImpl(ENVOY_LOGGER(), *dispatcher_)),
      listener_component_factory_(*this), worker_factory_(thread_local_, *api_, hooks),
      dns_resolver_(dispatcher_->createDnsResolver({})),
      access_log_manager_(*api_, *dispatcher_, access_log_lock, store), terminated_(false) {

  try {
    if (!options.logPath().empty()) {
      try {
        file_logger_ = std::make_unique<Logger::FileSinkDelegate>(
            options.logPath(), access_log_manager_, Logger::Registry::getSink());
      } catch (const EnvoyException& e) {
        throw EnvoyException(
            fmt::format("Failed to open log-file '{}'. e.what(): {}", options.logPath(), e.what()));
      }
    }

    restarter_.initialize(*dispatcher_, *this);
    drain_manager_ = component_factory.createDrainManager(*this);
    initialize(options, local_address, component_factory);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());

    terminate();
    throw;
  }
}

InstanceImpl::~InstanceImpl() {
  terminate();

  // Stop logging to file before all the AccessLogManager and its dependencies are
  // destructed to avoid crashing at shutdown.
  file_logger_.reset();
}

Upstream::ClusterManager& InstanceImpl::clusterManager() { return config_->clusterManager(); }

Tracing::HttpTracer& InstanceImpl::httpTracer() { return config_->httpTracer(); }

void InstanceImpl::drainListeners() {
  ENVOY_LOG(info, "closing and draining listeners");
  listener_manager_->stopListeners();
  drain_manager_->startDrainSequence(nullptr);
}

void InstanceImpl::failHealthcheck(bool fail) {
  // We keep liveness state in shared memory so the parent process sees the same state.
  server_stats_->live_.set(!fail);
}

void InstanceUtil::flushCountersAndGaugesToSinks(const std::list<Stats::SinkPtr>& sinks,
                                                 Stats::Store& store) {
  for (const auto& sink : sinks) {
    sink->beginFlush();
  }

  for (const Stats::CounterSharedPtr& counter : store.counters()) {
    uint64_t delta = counter->latch();
    if (counter->used()) {
      for (const auto& sink : sinks) {
        sink->flushCounter(*counter, delta);
      }
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : store.gauges()) {
    if (gauge->used()) {
      for (const auto& sink : sinks) {
        sink->flushGauge(*gauge, gauge->value());
      }
    }
  }

  for (const auto& sink : sinks) {
    sink->endFlush();
  }
}

void InstanceImpl::flushStats() {
  ENVOY_LOG(debug, "flushing stats");
  HotRestart::GetParentStatsInfo info;
  restarter_.getParentStats(info);
  server_stats_->uptime_.set(time(nullptr) - original_start_time_);
  server_stats_->memory_allocated_.set(Memory::Stats::totalCurrentlyAllocated() +
                                       info.memory_allocated_);
  server_stats_->memory_heap_size_.set(Memory::Stats::totalCurrentlyReserved());
  server_stats_->parent_connections_.set(info.num_connections_);
  server_stats_->total_connections_.set(numConnections() + info.num_connections_);
  server_stats_->days_until_first_cert_expiring_.set(
      sslContextManager().daysUntilFirstCertExpires());

  InstanceUtil::flushCountersAndGaugesToSinks(config_->statsSinks(), stats_store_);
  stat_flush_timer_->enableTimer(config_->statsFlushInterval());
}

void InstanceImpl::getParentStats(HotRestart::GetParentStatsInfo& info) {
  info.memory_allocated_ = Memory::Stats::totalCurrentlyAllocated();
  info.num_connections_ = numConnections();
}

bool InstanceImpl::healthCheckFailed() { return server_stats_->live_.value() == 0; }

InstanceUtil::BootstrapVersion
InstanceUtil::loadBootstrapConfig(envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                  Options& options) {
  try {
    if (!options.configPath().empty()) {
      MessageUtil::loadFromFile(options.configPath(), bootstrap);
    }
    if (!options.configYaml().empty()) {
      envoy::config::bootstrap::v2::Bootstrap bootstrap_override;
      MessageUtil::loadFromYaml(options.configYaml(), bootstrap_override);
      bootstrap.MergeFrom(bootstrap_override);
    }
    MessageUtil::validate(bootstrap);
    return BootstrapVersion::V2;
  } catch (const EnvoyException& e) {
    if (options.v2ConfigOnly()) {
      throw;
    }
    // TODO(htuch): When v1 is deprecated, make this a warning encouraging config upgrade.
    ENVOY_LOG(debug, "Unable to initialize config as v2, will retry as v1: {}", e.what());
  }
  if (!options.configYaml().empty()) {
    throw EnvoyException("V1 config (detected) with --config-yaml is not supported");
  }
  Json::ObjectSharedPtr config_json = Json::Factory::loadFromFile(options.configPath());
  Config::BootstrapJson::translateBootstrap(*config_json, bootstrap);
  MessageUtil::validate(bootstrap);
  return BootstrapVersion::V1;
}

void InstanceImpl::initialize(Options& options,
                              Network::Address::InstanceConstSharedPtr local_address,
                              ComponentFactory& component_factory) {
  ENVOY_LOG(info, "initializing epoch {} (hot restart version={})", options.restartEpoch(),
            restarter_.version());

  ENVOY_LOG(info, "statically linked extensions:");
  ENVOY_LOG(info, "  access_loggers: {}",
            Registry::FactoryRegistry<Configuration::AccessLogInstanceFactory>::allFactoryNames());
  ENVOY_LOG(
      info, "  filters.http: {}",
      Registry::FactoryRegistry<Configuration::NamedHttpFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  filters.listener: {}",
            Registry::FactoryRegistry<
                Configuration::NamedListenerFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(
      info, "  filters.network: {}",
      Registry::FactoryRegistry<Configuration::NamedNetworkFilterConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  stat_sinks: {}",
            Registry::FactoryRegistry<Configuration::StatsSinkFactory>::allFactoryNames());
  ENVOY_LOG(info, "  tracers: {}",
            Registry::FactoryRegistry<Configuration::HttpTracerFactory>::allFactoryNames());
  ENVOY_LOG(info, "  transport_sockets.downstream: {}",
            Registry::FactoryRegistry<
                Configuration::DownstreamTransportSocketConfigFactory>::allFactoryNames());
  ENVOY_LOG(info, "  transport_sockets.upstream: {}",
            Registry::FactoryRegistry<
                Configuration::UpstreamTransportSocketConfigFactory>::allFactoryNames());

  // Handle configuration that needs to take place prior to the main configuration load.
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  InstanceUtil::loadBootstrapConfig(bootstrap, options);

  // Needs to happen as early as possible in the instantiation to preempt the objects that require
  // stats.
  stats_store_.setTagProducer(Config::Utility::createTagProducer(bootstrap));

  server_stats_.reset(
      new ServerStats{ALL_SERVER_STATS(POOL_GAUGE_PREFIX(stats_store_, "server."))});

  failHealthcheck(false);

  uint64_t version_int;
  if (!StringUtil::atoul(VersionInfo::revision().substr(0, 6).c_str(), version_int, 16)) {
    throw EnvoyException("compiled GIT SHA is invalid. Invalid build.");
  }

  server_stats_->version_.set(version_int);
  bootstrap.mutable_node()->set_build_version(VersionInfo::version());

  local_info_.reset(
      new LocalInfo::LocalInfoImpl(bootstrap.node(), local_address, options.serviceZone(),
                                   options.serviceClusterName(), options.serviceNodeName()));

  Configuration::InitialImpl initial_config(bootstrap);
  ENVOY_LOG(debug, "admin address: {}", initial_config.admin().address()->asString());

  HotRestart::ShutdownParentAdminInfo info;
  info.original_start_time_ = original_start_time_;
  restarter_.shutdownParentAdmin(info);
  original_start_time_ = info.original_start_time_;
  admin_.reset(new AdminImpl(initial_config.admin().accessLogPath(),
                             initial_config.admin().profilePath(), options.adminAddressPath(),
                             initial_config.admin().address(), *this,
                             stats_store_.createScope("listener.admin.")));
  handler_->addListener(admin_->listener());

  loadServerFlags(initial_config.flagsPath());

  // Workers get created first so they register for thread local updates.
  listener_manager_.reset(
      new ListenerManagerImpl(*this, listener_component_factory_, worker_factory_));

  // The main thread is also registered for thread local updates so that code that does not care
  // whether it runs on the main thread or on workers can still use TLS.
  thread_local_.registerThread(*dispatcher_, true);

  // We can now initialize stats for threading.
  stats_store_.initializeThreading(*dispatcher_, thread_local_);

  // Runtime gets initialized before the main configuration since during main configuration
  // load things may grab a reference to the loader for later use.
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);

  // Once we have runtime we can initialize the SSL context manager.
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));

  cluster_manager_factory_.reset(new Upstream::ProdClusterManagerFactory(
      runtime(), stats(), threadLocal(), random(), dnsResolver(), sslContextManager(), dispatcher(),
      localInfo()));

  // Now the configuration gets parsed. The configuration may start setting thread local data
  // per above. See MainImpl::initialize() for why we do this pointer dance.
  Configuration::MainImpl* main_config = new Configuration::MainImpl();
  config_.reset(main_config);
  main_config->initialize(bootstrap, *this, *cluster_manager_factory_);

  for (Stats::SinkPtr& sink : main_config->statsSinks()) {
    stats_store_.addSink(*sink);
  }

  // Some of the stat sinks may need dispatcher support so don't flush until the main loop starts.
  // Just setup the timer.
  stat_flush_timer_ = dispatcher_->createTimer([this]() -> void { flushStats(); });
  stat_flush_timer_->enableTimer(config_->statsFlushInterval());

  // GuardDog (deadlock detection) object and thread setup before workers are
  // started and before our own run() loop runs.
  guard_dog_.reset(
      new Server::GuardDogImpl(stats_store_, *config_, ProdMonotonicTimeSource::instance_));
}

void InstanceImpl::startWorkers() {
  listener_manager_->startWorkers(*guard_dog_);

  // At this point we are ready to take traffic and all listening ports are up. Notify our parent
  // if applicable that they can stop listening and drain.
  restarter_.drainParentListeners();
  drain_manager_->startParentShutdownSequence();
}

Runtime::LoaderPtr InstanceUtil::createRuntime(Instance& server,
                                               Server::Configuration::Initial& config) {
  if (config.runtime()) {
    ENVOY_LOG(info, "runtime symlink: {}", config.runtime()->symlinkRoot());
    ENVOY_LOG(info, "runtime subdirectory: {}", config.runtime()->subdirectory());

    std::string override_subdirectory =
        config.runtime()->overrideSubdirectory() + "/" + server.localInfo().clusterName();
    ENVOY_LOG(info, "runtime override subdirectory: {}", override_subdirectory);

    Api::OsSysCallsPtr os_sys_calls(new Api::OsSysCallsImpl);
    return std::make_unique<Runtime::DiskBackedLoaderImpl>(
        server.dispatcher(), server.threadLocal(), config.runtime()->symlinkRoot(),
        config.runtime()->subdirectory(), override_subdirectory, server.stats(), server.random(),
        std::move(os_sys_calls));
  } else {
    return std::make_unique<Runtime::LoaderImpl>(server.random(), server.stats(),
                                                 server.threadLocal());
  }
}

void InstanceImpl::loadServerFlags(const absl::optional<std::string>& flags_path) {
  if (!flags_path) {
    return;
  }

  ENVOY_LOG(info, "server flags path: {}", flags_path.value());
  if (api_->fileExists(flags_path.value() + "/drain")) {
    ENVOY_LOG(info, "starting server in drain mode");
    failHealthcheck(true);
  }
}

uint64_t InstanceImpl::numConnections() { return listener_manager_->numConnections(); }

RunHelper::RunHelper(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                     HotRestart& hot_restart, AccessLog::AccessLogManager& access_log_manager,
                     InitManagerImpl& init_manager, std::function<void()> workers_start_cb) {

  // Setup signals.
  sigterm_ = dispatcher.listenForSignal(SIGTERM, [this, &hot_restart, &dispatcher]() {
    ENVOY_LOG(warn, "caught SIGTERM");
    shutdown_ = true;
    hot_restart.terminateParent();
    dispatcher.exit();
  });

  sig_usr_1_ = dispatcher.listenForSignal(SIGUSR1, [&access_log_manager]() {
    ENVOY_LOG(warn, "caught SIGUSR1");
    access_log_manager.reopen();
  });

  sig_hup_ = dispatcher.listenForSignal(SIGHUP, []() {
    ENVOY_LOG(warn, "caught and eating SIGHUP. See documentation for how to hot restart.");
  });

  // Register for cluster manager init notification. We don't start serving worker traffic until
  // upstream clusters are initialized which may involve running the event loop. Note however that
  // this can fire immediately if all clusters have already initialized. Also note that we need
  // to guard against shutdown at two different levels since SIGTERM can come in once the run loop
  // starts.
  cm.setInitializedCb([this, &init_manager, workers_start_cb]() {
    if (shutdown_) {
      return;
    }

    ENVOY_LOG(info, "all clusters initialized. initializing init manager");
    init_manager.initialize([this, workers_start_cb]() {
      if (shutdown_) {
        return;
      }

      workers_start_cb();
    });
  });
}

void InstanceImpl::run() {
  RunHelper helper(*dispatcher_, clusterManager(), restarter_, access_log_manager_, init_manager_,
                   [this]() -> void { startWorkers(); });

  // Run the main dispatch loop waiting to exit.
  ENVOY_LOG(info, "starting main dispatch loop");
  auto watchdog = guard_dog_->createWatchDog(Thread::Thread::currentThreadId());
  watchdog->startWatchdog(*dispatcher_);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  ENVOY_LOG(info, "main dispatch loop exited");
  guard_dog_->stopWatching(watchdog);
  watchdog.reset();

  terminate();
}

void InstanceImpl::terminate() {
  if (terminated_) {
    return;
  }
  terminated_ = true;

  // Before starting to shutdown anything else, stop slot destruction updates.
  thread_local_.shutdownGlobalThreading();

  // Before the workers start exiting we should disable stat threading.
  stats_store_.shutdownThreading();

  // Shutdown all the workers now that the main dispatch loop is done.
  if (listener_manager_.get() != nullptr) {
    listener_manager_->stopWorkers();
  }

  // Only flush if we have not been hot restarted.
  if (stat_flush_timer_) {
    flushStats();
  }

  if (config_.get() != nullptr) {
    config_->clusterManager().shutdown();
  }
  handler_.reset();
  thread_local_.shutdownThread();
  restarter_.shutdown();
  ENVOY_LOG(info, "exiting");
  ENVOY_FLUSH_LOG();
}

Runtime::Loader& InstanceImpl::runtime() { return *runtime_loader_; }

void InstanceImpl::shutdown() {
  ENVOY_LOG(info, "shutdown invoked. sending SIGTERM to self");
  kill(getpid(), SIGTERM);
}

void InstanceImpl::shutdownAdmin() {
  ENVOY_LOG(warn, "shutting down admin due to child startup");
  stat_flush_timer_.reset();
  handler_->stopListeners();
  admin_->mutable_socket().close();

  ENVOY_LOG(warn, "terminating parent process");
  restarter_.terminateParent();
}

} // namespace Server
} // namespace Envoy
