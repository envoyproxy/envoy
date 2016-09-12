#include "configuration_impl.h"
#include "server.h"
#include "test_hooks.h"
#include "worker.h"

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/network/dns.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/access_log/access_log_manager.h"
#include "common/common/version.h"
#include "common/memory/stats.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/stats_impl.h"
#include "common/stats/statsd.h"

namespace Server {

InstanceImpl::InstanceImpl(Options& options, TestHooks& hooks, HotRestart& restarter,
                           Stats::Store& store, Thread::BasicLockable& access_log_lock,
                           ComponentFactory& component_factory)
    : options_(options), restarter_(restarter), start_time_(time(nullptr)),
      original_start_time_(start_time_), stats_store_(store), access_log_lock_(access_log_lock),
      server_stats_{ALL_SERVER_STATS(POOL_GAUGE_PREFIX(stats_store_, "server."))},
      handler_(stats_store_, log(), options.flushIntervalMsec()), dns_resolver_(handler_.dispatcher().createDnsResolver()),
      local_address_(Network::Utility::getLocalAddress()) {

  failHealthcheck(false);

  uint64_t version_int;
  if (!StringUtil::atoul(VersionInfo::GIT_SHA.substr(0, 6).c_str(), version_int, 16)) {
    throw EnvoyException("compiled GIT SHA is invalid. Invalid build.");
  }
  server_stats_.version_.set(version_int);

  if (local_address_.empty()) {
    throw EnvoyException("could not resolve local address");
  }

  restarter_.initialize(handler_.dispatcher(), *this);
  drain_manager_ = component_factory.createDrainManager(*this);

  try {
    initialize(options, hooks, component_factory);
  } catch (const EnvoyException& e) {
    log().emerg("error initializing configuration '{}': {}", options.configPath(), e.what());
    exit(1);
  }
}

Upstream::ClusterManager& InstanceImpl::clusterManager() { return config_->clusterManager(); }

Tracing::HttpTracer& InstanceImpl::httpTracer() { return config_->httpTracer(); }

void InstanceImpl::drainListeners() {
  log().notice("closing and draining listeners");
  for (const auto& worker : workers_) {
    Worker& worker_ref = *worker;
    worker->dispatcher().post([&worker_ref]() -> void { worker_ref.handler()->closeListeners(); });
  }

  drain_manager_->startDrainSequence();
}

void InstanceImpl::failHealthcheck(bool fail) {
  // We keep liveness state in shared memory so the parent process sees the same state.
  server_stats_.live_.set(!fail);
}

void InstanceImpl::flushStats() {
  log_debug("flushing stats");
  HotRestart::GetParentStatsInfo info;
  restarter_.getParentStats(info);
  server_stats_.uptime_.set(time(nullptr) - original_start_time_);
  server_stats_.memory_allocated_.set(Memory::Stats::totalCurrentlyAllocated() +
                                      info.memory_allocated_);
  server_stats_.memory_heap_size_.set(Memory::Stats::totalCurrentlyReserved());
  server_stats_.parent_connections_.set(info.num_connections_);
  server_stats_.total_connections_.set(numConnections() + info.num_connections_);
  server_stats_.days_until_first_cert_expiring_.set(
      sslContextManager().daysUntilFirstCertExpires());

  for (Stats::Counter& counter : stats_store_.counters()) {
    uint64_t delta = counter.latch();
    if (counter.used()) {
      for (const auto& sink : stat_sinks_) {
        sink->flushCounter(counter.name(), delta);
      }
    }
  }

  for (Stats::Gauge& gauge : stats_store_.gauges()) {
    if (gauge.used()) {
      for (const auto& sink : stat_sinks_) {
        sink->flushGauge(gauge.name(), gauge.value());
      }
    }
  }

  stat_flush_timer_->enableTimer(std::chrono::milliseconds(5000));
}

int InstanceImpl::getListenSocketFd(uint32_t port) {
  for (const auto& entry : socket_map_) {
    if (entry.second->port() == port) {
      return entry.second->fd();
    }
  }

  return -1;
}

void InstanceImpl::getParentStats(HotRestart::GetParentStatsInfo& info) {
  info.memory_allocated_ = Memory::Stats::totalCurrentlyAllocated();
  info.num_connections_ = numConnections();
}

bool InstanceImpl::healthCheckFailed() { return server_stats_.live_.value() == 0; }

void InstanceImpl::initialize(Options& options, TestHooks& hooks,
                              ComponentFactory& component_factory) {
  log().notice("initializing epoch {} (hot restart version={})", options.restartEpoch(),
               restarter_.version());

  // Handle configuration that needs to take place prior to the main configuration load.
  Configuration::InitialImpl initial_config(options.configPath());
  log().notice("admin port: {}", initial_config.admin().port());

  HotRestart::ShutdownParentAdminInfo info;
  info.original_start_time_ = original_start_time_;
  restarter_.shutdownParentAdmin(info);
  drain_manager_->startParentShutdownSequence();
  original_start_time_ = info.original_start_time_;
  admin_.reset(
      new AdminImpl(initial_config.admin().accessLogPath(), initial_config.admin().port(), *this));
  handler_.addListener(*admin_, admin_->socket(), false);

  loadServerFlags(initial_config.flagsPath());

  // Workers get created first so they register for thread local updates.
  for (uint32_t i = 0; i < std::max(1U, options.concurrency()); i++) {
    workers_.emplace_back(new Worker(stats_store_, thread_local_, options.flushIntervalMsec()));
  }

  // The main thread is also registered for thread local updates so that code that does not care
  // whether it runs on the main thread or on workers can still use TLS.
  thread_local_.registerThread(handler_.dispatcher(), true);

  // Runtime gets initialized before the main configuration since during main configuration
  // load things may grab a reference to the loader for later use.
  runtime_loader_ = component_factory.createRuntime(*this, initial_config);

  // Once we have runtime we can initialize the SSL context manager.
  ssl_context_manager_.reset(new Ssl::ContextManagerImpl(*runtime_loader_));

  // Now the configuration gets parsed. The configuration may start setting thread local data
  // per above. See MainImpl::initialize() for why we do this pointer dance.
  Configuration::MainImpl* main_config = new Configuration::MainImpl(*this);
  config_.reset(main_config);
  main_config->initialize(options.configPath());

  for (const Configuration::ListenerPtr& listener : config_->listeners()) {
    // For each listener config we share a single TcpListenSocket among all threaded listeners.
    // UdsListenerSockets are not managed and do not participate in hot restart as they are only
    // used for testing.

    // First we try to get the socket from our parent if applicable.
    int fd = restarter_.duplicateParentListenSocket(listener->port());
    if (fd != -1) {
      log().info("obtained socket for port {} from parent", listener->port());
      socket_map_[listener.get()].reset(new Network::TcpListenSocket(fd, listener->port()));
    } else {
      socket_map_[listener.get()].reset(new Network::TcpListenSocket(listener->port()));
    }
  }

  // Setup signals.
  sigterm_ = handler_.dispatcher().listenForSignal(SIGTERM, [this]() -> void {
    log().notice("caught SIGTERM");
    restarter_.terminateParent();
    handler_.dispatcher().exit();
  });

  sig_usr_1_ = handler_.dispatcher().listenForSignal(SIGUSR1, [this]() -> void {
    log().notice("caught SIGUSR1");
    access_log_manager_.reopen();
  });

  sig_hup_ = handler_.dispatcher().listenForSignal(SIGHUP, [this]() -> void {
    log().notice("caught and eating SIGHUP. See documentation for how to hot restart.");
  });

  // Register for cluster manager init notification. We don't start serving worker traffic until
  // upstream clusters are initialized which may involve running the event loop. Note however that
  // if there are only static clusters this will fire immediately.
  clusterManager().setInitializedCb([this, &hooks]() -> void {
    log().notice("all clusters initialized. starting workers");
    for (const WorkerPtr& worker : workers_) {
      try {
        worker->initializeConfiguration(*config_, socket_map_);
      } catch (const Network::CreateListenerException& e) {
        // It is possible that we fail to start listening on a port, even though we were able to
        // bind to it above. This happens when there is a race between two applications to listen
        // on the same port. In general if we can't initialize the worker configuration just print
        // the error and exit cleanly without crashing.
        log().emerg("shutting down due to error initializing worker configuration: {}", e.what());
        shutdown();
      }
    }

    // At this point we are ready to take traffic and all listening ports are up. Notify our parent
    // if applicable that they can stop listening and drain.
    restarter_.drainParentListeners();
    hooks.onServerInitialized();
  });

  initializeStatSinks();

  // Some of the stat sinks may need dispatcher support so don't flush until the main loop starts.
  // Just setup the timer.
  stat_flush_timer_ = handler_.dispatcher().createTimer([this]() -> void { flushStats(); });
  stat_flush_timer_->enableTimer(std::chrono::milliseconds(5000));
}

Runtime::LoaderPtr InstanceUtil::createRuntime(Instance& server,
                                               Server::Configuration::Initial& config) {
  if (config.runtime()) {
    log().notice("runtime symlink: {}", config.runtime()->symlinkRoot());
    log().notice("runtime subdirectory: {}", config.runtime()->subdirectory());

    std::string override_subdirectory =
        config.runtime()->overrideSubdirectory() + "/" + server.options().serviceClusterName();
    log().notice("runtime override subdirectory: {}", override_subdirectory);

    return Runtime::LoaderPtr{new Runtime::LoaderImpl(
        server.dispatcher(), server.threadLocal(), config.runtime()->symlinkRoot(),
        config.runtime()->subdirectory(), override_subdirectory, server.stats(), server.random())};
  } else {
    return Runtime::LoaderPtr{new Runtime::NullLoaderImpl(server.random())};
  }
}

void InstanceImpl::initializeStatSinks() {
  if (config_->statsdUdpPort().valid()) {
    log().notice("statsd UDP port: {}", config_->statsdUdpPort().value());
    stat_sinks_.emplace_back(new Stats::Statsd::UdpStatsdSink(config_->statsdUdpPort().value()));
    stats_store_.addSink(*stat_sinks_.back());
  }

  if (config_->statsdTcpClusterName().valid()) {
    log().notice("statsd TCP cluster: {}", config_->statsdTcpClusterName().value());
    stat_sinks_.emplace_back(new Stats::Statsd::TcpStatsdSink(
        options_.serviceClusterName(), options_.serviceNodeName(),
        config_->statsdTcpClusterName().value(), thread_local_, config_->clusterManager()));
    stats_store_.addSink(*stat_sinks_.back());
  }
}

void InstanceImpl::loadServerFlags(const Optional<std::string>& flags_path) {
  if (!flags_path.valid()) {
    return;
  }

  log().notice("server flags path: {}", flags_path.value());
  if (handler_.api().fileExists(flags_path.value() + "/drain")) {
    log().notice("starting server in drain mode");
    failHealthcheck(true);
  }
}

uint64_t InstanceImpl::numConnections() {
  uint64_t num_connections = 0;
  for (const auto& worker : workers_) {
    if (worker->handler()) {
      num_connections += worker->handler()->numConnections();
    }
  }

  return num_connections;
}

void InstanceImpl::run() {
  // Run the main dispatch loop waiting to exit.
  log().notice("starting main dispatch loop");
  handler_.startWatchdog();
  handler_.dispatcher().run(Event::Dispatcher::RunType::Block);
  log().notice("main dispatch loop exited");

  // Shutdown all the listeners now that the main dispatch loop is done.
  for (const WorkerPtr& worker : workers_) {
    worker->exit();
  }

  // Only flush if we have not been hot restarted.
  if (stat_flush_timer_) {
    flushStats();
  }

  config_->clusterManager().shutdown();
  handler_.closeConnections();
  thread_local_.shutdownThread();
  log().notice("exiting");
  log().flush();
}

Runtime::Loader& InstanceImpl::runtime() { return *runtime_loader_; }

InstanceImpl::~InstanceImpl() {}

void InstanceImpl::shutdown() {
  log().notice("shutdown invoked. sending SIGTERM to self");
  kill(getpid(), SIGTERM);
}

void InstanceImpl::shutdownAdmin() {
  log().notice("shutting down admin due to child startup");
  stat_flush_timer_.reset();
  handler_.closeListeners();
  admin_->socket().close();

  log().notice("terminating parent process");
  restarter_.terminateParent();
}

} // Server
