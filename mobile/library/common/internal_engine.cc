#include "library/common/internal_engine.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/lock_guard.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/synchronization/notification.h"
#include "library/common/bridge/utility.h"
#include "library/common/data/utility.h"
#include "library/common/stats/utility.h"

namespace Envoy {

static std::atomic<envoy_stream_t> current_stream_handle_{0};

envoy_stream_t InternalEngine::initStream() { return current_stream_handle_++; }

InternalEngine::InternalEngine(envoy_engine_callbacks callbacks, envoy_logger logger,
                               envoy_event_tracker event_tracker,
                               Thread::PosixThreadFactoryPtr thread_factory)
    : thread_factory_(std::move(thread_factory)), callbacks_(callbacks), logger_(logger),
      event_tracker_(event_tracker), dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()) {
  ExtensionRegistry::registerFactories();

  // TODO(Augustyniak): Capturing an address of event_tracker_ and registering it in the API
  // registry may lead to crashes at Engine shutdown. To be figured out as part of
  // https://github.com/envoyproxy/envoy-mobile/issues/332
  Envoy::Api::External::registerApi(std::string(envoy_event_tracker_api_name), &event_tracker_);
  // Envoy Mobile always requires dfp_mixed_scheme for the TLS and cleartext DFP clusters.
  // While dfp_mixed_scheme defaults to true, some environments force it to false (e.g. within
  // Google), so we force it back to true in Envoy Mobile.
  // TODO(abeyad): Remove once this is no longer needed.
  Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.dfp_mixed_scheme", true);
}

InternalEngine::InternalEngine(envoy_engine_callbacks callbacks, envoy_logger logger,
                               envoy_event_tracker event_tracker)
    : InternalEngine(callbacks, logger, event_tracker, Thread::PosixThreadFactory::create()) {}

envoy_status_t InternalEngine::run(const std::string& config, const std::string& log_level) {
  // Start the Envoy on the dedicated thread. Note: due to how the assignment operator works with
  // std::thread, main_thread_ is the same object after this call, but its state is replaced with
  // that of the temporary. The temporary object's state becomes the default state, which does
  // nothing.
  auto options = std::make_shared<Envoy::OptionsImplBase>();
  options->setConfigYaml(config);
  if (!log_level.empty()) {
    ENVOY_BUG(options->setLogLevel(log_level).ok(), "invalid log level");
  }
  options->setConcurrency(1);
  return run(std::move(options));
}

// This function takes a `std::shared_ptr` instead of `std::unique_ptr` because `std::function` is a
// copy-constructible type, so it's not possible to move capture `std::unique_ptr` with
// `std::function`.
envoy_status_t InternalEngine::run(std::shared_ptr<Envoy::OptionsImplBase> options) {
  main_thread_ =
      thread_factory_->createThread([this, options]() mutable -> void { main(options); },
                                    /* options= */ absl::nullopt, /* crash_on_failure= */ false);
  return (main_thread_ != nullptr) ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t InternalEngine::main(std::shared_ptr<Envoy::OptionsImplBase> options) {
  // Using unique_ptr ensures main_common's lifespan is strictly scoped to this function.
  std::unique_ptr<EngineCommon> main_common;
  {
    Thread::LockGuard lock(mutex_);
    TRY_NEEDS_AUDIT {
      if (event_tracker_.track != nullptr) {
        assert_handler_registration_ =
            Assert::addDebugAssertionFailureRecordAction([this](const char* location) {
              const auto event = Bridge::Utility::makeEnvoyMap(
                  {{"name", "assertion"}, {"location", std::string(location)}});
              event_tracker_.track(event, event_tracker_.context);
            });
        bug_handler_registration_ =
            Assert::addEnvoyBugFailureRecordAction([this](const char* location) {
              const auto event = Bridge::Utility::makeEnvoyMap(
                  {{"name", "bug"}, {"location", std::string(location)}});
              event_tracker_.track(event, event_tracker_.context);
            });
      }

      // We let the thread clean up this log delegate pointer
      if (logger_.log) {
        log_delegate_ptr_ =
            std::make_unique<Logger::LambdaDelegate>(logger_, Logger::Registry::getSink());
      } else {
        log_delegate_ptr_ =
            std::make_unique<Logger::DefaultDelegate>(log_mutex_, Logger::Registry::getSink());
      }

      main_common = std::make_unique<EngineCommon>(options);
      server_ = main_common->server();
      event_dispatcher_ = &server_->dispatcher();

      cv_.notifyAll();
    }
    END_TRY
    CATCH(const Envoy::EnvoyException& e, { PANIC(e.what()); });

    // Note: We're waiting longer than we might otherwise to drain to the main thread's dispatcher.
    // This is because we're not simply waiting for its availability and for it to have started, but
    // also because we're waiting for clusters to have done their first attempt at DNS resolution.
    // When we improve synchronous failure handling and/or move to dynamic forwarding, we only need
    // to wait until the dispatcher is running (and can drain by enqueueing a drain callback on it,
    // as we did previously).

    postinit_callback_handler_ = main_common->server()->lifecycleNotifier().registerCallback(
        Envoy::Server::ServerLifecycleNotifier::Stage::PostInit, [this]() -> void {
          ASSERT(Thread::MainThread::isMainOrTestThread());

          Envoy::Server::GenericFactoryContextImpl generic_context(
              server_->serverFactoryContext(),
              server_->serverFactoryContext().messageValidationVisitor());
          connectivity_manager_ = Network::ConnectivityManagerFactory{generic_context}.get();
          auto v4_interfaces = connectivity_manager_->enumerateV4Interfaces();
          auto v6_interfaces = connectivity_manager_->enumerateV6Interfaces();
          logInterfaces("netconf_get_v4_interfaces", v4_interfaces);
          logInterfaces("netconf_get_v6_interfaces", v6_interfaces);
          client_scope_ = server_->serverFactoryContext().scope().createScope("pulse.");
          // StatNameSet is lock-free, the benefit of using it is being able to create StatsName
          // on-the-fly without risking contention on system with lots of threads.
          // It also comes with ease of programming.
          stat_name_set_ = client_scope_->symbolTable().makeSet("pulse");
          auto api_listener = server_->listenerManager().apiListener()->get().createHttpApiListener(
              server_->dispatcher());
          ASSERT(api_listener != nullptr);
          http_client_ = std::make_unique<Http::Client>(std::move(api_listener), *dispatcher_,
                                                        server_->serverFactoryContext().scope(),
                                                        server_->api().randomGenerator());
          dispatcher_->drain(server_->dispatcher());
          if (callbacks_.on_engine_running != nullptr) {
            callbacks_.on_engine_running(callbacks_.context);
          }
        });
  } // mutex_

  // The main run loop must run without holding the mutex, so that the destructor can acquire it.
  bool run_success = main_common->run();
  // The above call is blocking; at this point the event loop has exited.

  // Ensure destructors run on Envoy's main thread.
  postinit_callback_handler_.reset(nullptr);
  connectivity_manager_.reset();
  client_scope_.reset();
  stat_name_set_.reset();
  main_common.reset(nullptr);
  bug_handler_registration_.reset(nullptr);
  assert_handler_registration_.reset(nullptr);

  callbacks_.on_exit(callbacks_.context);

  return run_success ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t InternalEngine::terminate() {
  if (terminated_) {
    IS_ENVOY_BUG("attempted to double terminate engine");
    return ENVOY_FAILURE;
  }
  // The Engine could not be created.
  if (main_thread_ == nullptr) {
    return ENVOY_FAILURE;
  }
  // If main_thread_ has finished (or hasn't started), there's nothing more to do.
  if (!main_thread_->joinable()) {
    return ENVOY_FAILURE;
  }

  // We need to be sure that MainCommon is finished being constructed so we can dispatch shutdown.
  {
    Thread::LockGuard lock(mutex_);

    if (!event_dispatcher_) {
      cv_.wait(mutex_);
    }

    ASSERT(event_dispatcher_);
    ASSERT(dispatcher_);

    // We must destroy the Http::ApiListener in the main thread.
    dispatcher_->post([this]() { http_client_->shutdownApiListener(); });

    // Exit the event loop and finish up in Engine::run(...)
    if (thread_factory_->currentPthreadId() == main_thread_->pthreadId()) {
      // TODO(goaway): figure out some way to support this.
      PANIC("Terminating the engine from its own main thread is currently unsupported.");
    } else {
      dispatcher_->terminate();
    }
  } // lock(_mutex)

  if (thread_factory_->currentPthreadId() != main_thread_->pthreadId()) {
    main_thread_->join();
  }
  terminated_ = true;
  return ENVOY_SUCCESS;
}

bool InternalEngine::isTerminated() const { return terminated_; }

InternalEngine::~InternalEngine() {
  if (!terminated_) {
    terminate();
  }
}

envoy_status_t InternalEngine::setProxySettings(const char* hostname, const uint16_t port) {
  return dispatcher_->post([&, host = std::string(hostname), port]() -> void {
    connectivity_manager_->setProxySettings(Network::ProxySettings::parseHostAndPort(host, port));
  });
}

envoy_status_t InternalEngine::resetConnectivityState() {
  return dispatcher_->post([&]() -> void { connectivity_manager_->resetConnectivityState(); });
}

envoy_status_t InternalEngine::setPreferredNetwork(envoy_network_t network) {
  return dispatcher_->post([&, network]() -> void {
    envoy_netconf_t configuration_key =
        Envoy::Network::ConnectivityManagerImpl::setPreferredNetwork(network);
    connectivity_manager_->refreshDns(configuration_key, true);
  });
}

envoy_status_t InternalEngine::recordCounterInc(absl::string_view elements, envoy_stats_tags tags,
                                                uint64_t count) {
  return dispatcher_->post(
      [&, name = Stats::Utility::sanitizeStatsName(elements), tags, count]() -> void {
        ENVOY_LOG(trace, "[pulse.{}] recordCounterInc", name);
        Stats::StatNameTagVector tags_vctr =
            Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
        Stats::Utility::counterFromElements(*client_scope_, {Stats::DynamicName(name)}, tags_vctr)
            .add(count);
      });
}

Event::ProvisionalDispatcher& InternalEngine::dispatcher() { return *dispatcher_; }

void statsAsText(const std::map<std::string, uint64_t>& all_stats,
                 const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                 Buffer::Instance& response) {
  for (const auto& stat : all_stats) {
    response.addFragments({stat.first, ": ", absl::StrCat(stat.second), "\n"});
  }
  std::map<std::string, std::string> all_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : histograms) {
    if (histogram->used()) {
      all_histograms.emplace(histogram->name(), histogram->quantileSummary());
    }
  }
  for (const auto& histogram : all_histograms) {
    response.addFragments({histogram.first, ": ", histogram.second, "\n"});
  }
}

void handlerStats(Stats::Store& stats, Buffer::Instance& response) {
  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : stats.counters()) {
    if (counter->used()) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : stats.gauges()) {
    if (gauge->used()) {
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  std::vector<Stats::ParentHistogramSharedPtr> histograms = stats.histograms();
  stats.symbolTable().sortByStatNames<Stats::ParentHistogramSharedPtr>(
      histograms.begin(), histograms.end(),
      [](const Stats::ParentHistogramSharedPtr& a) -> Stats::StatName { return a->statName(); });

  statsAsText(all_stats, histograms, response);
}

std::string InternalEngine::dumpStats() {
  if (!main_thread_->joinable()) {
    return "";
  }

  std::string stats;
  absl::Notification stats_received;
  if (dispatcher_->post([&]() -> void {
        Envoy::Buffer::OwnedImpl instance;
        handlerStats(server_->stats(), instance);
        stats = instance.toString();
        stats_received.Notify();
      }) == ENVOY_SUCCESS) {
    stats_received.WaitForNotification();
    return stats;
  }
  return stats;
}

Upstream::ClusterManager& InternalEngine::getClusterManager() {
  ASSERT(dispatcher_->isThreadSafe(),
         "getClusterManager must be called from the dispatcher's context");
  return server_->clusterManager();
}

Stats::Store& InternalEngine::getStatsStore() {
  ASSERT(dispatcher_->isThreadSafe(), "getStatsStore must be called from the dispatcher's context");
  return server_->stats();
}

void InternalEngine::logInterfaces(absl::string_view event,
                                   std::vector<Network::InterfacePair>& interfaces) {
  std::vector<std::string> names;
  names.resize(interfaces.size());
  std::transform(interfaces.begin(), interfaces.end(), names.begin(),
                 [](Network::InterfacePair& pair) { return std::get<0>(pair); });

  auto unique_end = std::unique(names.begin(), names.end());
  std::string all_names = std::accumulate(names.begin(), unique_end, std::string{},
                                          [](std::string acc, std::string next) {
                                            return acc.empty() ? next : std::move(acc) + "," + next;
                                          });
  ENVOY_LOG_EVENT(debug, event, all_names);
}

} // namespace Envoy
