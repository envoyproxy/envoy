#include "library/common/engine.h"

#include "envoy/stats/histogram.h"

#include "source/common/common/lock_guard.h"

#include "library/common/bridge/utility.h"
#include "library/common/config/internal.h"
#include "library/common/data/utility.h"
#include "library/common/network/mobile_utility.h"
#include "library/common/stats/utility.h"
#include "types/c_types.h"

namespace Envoy {

Engine::Engine(envoy_engine_callbacks callbacks, envoy_logger logger,
               envoy_event_tracker event_tracker, std::atomic<envoy_network_t>& preferred_network)
    : callbacks_(callbacks), logger_(logger), event_tracker_(event_tracker),
      dispatcher_(std::make_unique<Event::ProvisionalDispatcher>()),
      preferred_network_(preferred_network) {
  // Ensure static factory registration occurs on time.
  // TODO: ensure this is only called one time once multiple Engine objects can be allocated.
  // https://github.com/lyft/envoy-mobile/issues/332
  ExtensionRegistry::registerFactories();

  // TODO(Augustyniak): Capturing an address of event_tracker_ and registering it in the API
  // registry may lead to crashes at Engine shutdown. To be figured out as part of
  // https://github.com/lyft/envoy-mobile/issues/332
  Envoy::Api::External::registerApi(std::string(envoy_event_tracker_api_name), &event_tracker_);
}

envoy_status_t Engine::run(const std::string config, const std::string log_level) {
  // Start the Envoy on the dedicated thread. Note: due to how the assignment operator works with
  // std::thread, main_thread_ is the same object after this call, but its state is replaced with
  // that of the temporary. The temporary object's state becomes the default state, which does
  // nothing.
  main_thread_ = std::thread(&Engine::main, this, std::string(config), std::string(log_level));
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::main(const std::string config, const std::string log_level) {
  // Using unique_ptr ensures main_common's lifespan is strictly scoped to this function.
  std::unique_ptr<EngineCommon> main_common;
  const std::string name = "envoy";
  const std::string config_flag = "--config-yaml";
  const std::string composed_config = absl::StrCat(config_header, config);
  const std::string log_flag = "-l";
  const std::string concurrency_option = "--concurrency";
  const std::string concurrency_arg = "0";
  std::vector<const char*> envoy_argv = {name.c_str(),
                                         config_flag.c_str(),
                                         composed_config.c_str(),
                                         concurrency_option.c_str(),
                                         concurrency_arg.c_str(),
                                         log_flag.c_str(),
                                         log_level.c_str(),
                                         nullptr};
  {
    Thread::LockGuard lock(mutex_);
    try {
      if (event_tracker_.track != nullptr) {
        assert_handler_registration_ =
            Assert::addDebugAssertionFailureRecordAction([this](const char* location) {
              const auto event = Bridge::Utility::makeEnvoyMap(
                  {{"name", "assertion"}, {"location", std::string(location)}});
              event_tracker_.track(event, event_tracker_.context);
            });
      }

      main_common = std::make_unique<EngineCommon>(envoy_argv.size() - 1, envoy_argv.data());
      server_ = main_common->server();
      event_dispatcher_ = &server_->dispatcher();

      if (logger_.log) {
        log_delegate_ptr_ =
            std::make_unique<Logger::LambdaDelegate>(logger_, Logger::Registry::getSink());
      } else {
        log_delegate_ptr_ =
            std::make_unique<Logger::DefaultDelegate>(log_mutex_, Logger::Registry::getSink());
      }

      cv_.notifyAll();
    } catch (const Envoy::NoServingException& e) {
      PANIC(e.what());
    } catch (const Envoy::MalformedArgvException& e) {
      PANIC(e.what());
    } catch (const Envoy::EnvoyException& e) {
      PANIC(e.what());
    }

    // Note: We're waiting longer than we might otherwise to drain to the main thread's dispatcher.
    // This is because we're not simply waiting for its availability and for it to have started, but
    // also because we're waiting for clusters to have done their first attempt at DNS resolution.
    // When we improve synchronous failure handling and/or move to dynamic forwarding, we only need
    // to wait until the dispatcher is running (and can drain by enqueueing a drain callback on it,
    // as we did previously).

    postinit_callback_handler_ = main_common->server()->lifecycleNotifier().registerCallback(
        Envoy::Server::ServerLifecycleNotifier::Stage::PostInit, [this]() -> void {
          ASSERT(Thread::MainThread::isMainThread());

          logInterfaces();

          client_scope_ = server_->serverFactoryContext().scope().createScope("pulse.");
          // StatNameSet is lock-free, the benefit of using it is being able to create StatsName
          // on-the-fly without risking contention on system with lots of threads.
          // It also comes with ease of programming.
          stat_name_set_ = client_scope_->symbolTable().makeSet("pulse");
          auto api_listener = server_->listenerManager().apiListener()->get().http();
          ASSERT(api_listener.has_value());
          http_client_ = std::make_unique<Http::Client>(
              api_listener.value(), *dispatcher_, server_->serverFactoryContext().scope(),
              preferred_network_, server_->api().randomGenerator());
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
  client_scope_.reset(nullptr);
  stat_name_set_.reset();
  log_delegate_ptr_.reset(nullptr);
  main_common.reset(nullptr);
  assert_handler_registration_.reset(nullptr);

  callbacks_.on_exit(callbacks_.context);

  return run_success ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

envoy_status_t Engine::terminate() {
  // If main_thread_ has finished (or hasn't started), there's nothing more to do.
  if (!main_thread_.joinable()) {
    return ENVOY_FAILURE;
  }

  // We need to be sure that MainCommon is finished being constructed so we can dispatch shutdown.
  {
    Thread::LockGuard lock(mutex_);

    if (!event_dispatcher_) {
      cv_.wait(mutex_);
    }

    ASSERT(event_dispatcher_);

    // Exit the event loop and finish up in Engine::run(...)
    if (std::this_thread::get_id() == main_thread_.get_id()) {
      // TODO(goaway): figure out some way to support this.
      PANIC("Terminating the engine from its own main thread is currently unsupported.");
    } else {
      event_dispatcher_->exit();
    }
  } // lock(_mutex)

  if (std::this_thread::get_id() != main_thread_.get_id()) {
    main_thread_.join();
  }

  return ENVOY_SUCCESS;
}

Engine::~Engine() { terminate(); }

envoy_status_t Engine::recordCounterInc(const std::string& elements, envoy_stats_tags tags,
                                        uint64_t count) {
  ENVOY_LOG(trace, "[pulse.{}] recordCounterInc", elements);
  ASSERT(dispatcher_->isThreadSafe(), "pulse calls must run from dispatcher's context");
  Stats::StatNameTagVector tags_vctr =
      Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
  std::string name = Stats::Utility::sanitizeStatsName(elements);
  Stats::Utility::counterFromElements(*client_scope_, {Stats::DynamicName(name)}, tags_vctr)
      .add(count);
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::recordGaugeSet(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t value) {
  ENVOY_LOG(trace, "[pulse.{}] recordGaugeSet", elements);
  ASSERT(dispatcher_->isThreadSafe(), "pulse calls must run from dispatcher's context");
  Stats::StatNameTagVector tags_vctr =
      Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
  std::string name = Stats::Utility::sanitizeStatsName(elements);
  Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                    Stats::Gauge::ImportMode::NeverImport, tags_vctr)
      .set(value);
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::recordGaugeAdd(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t amount) {
  ENVOY_LOG(trace, "[pulse.{}] recordGaugeAdd", elements);
  ASSERT(dispatcher_->isThreadSafe(), "pulse calls must run from dispatcher's context");
  Stats::StatNameTagVector tags_vctr =
      Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
  std::string name = Stats::Utility::sanitizeStatsName(elements);
  Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                    Stats::Gauge::ImportMode::NeverImport, tags_vctr)
      .add(amount);
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::recordGaugeSub(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t amount) {
  ENVOY_LOG(trace, "[pulse.{}] recordGaugeSub", elements);
  ASSERT(dispatcher_->isThreadSafe(), "pulse calls must run from dispatcher's context");
  Stats::StatNameTagVector tags_vctr =
      Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
  std::string name = Stats::Utility::sanitizeStatsName(elements);
  Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                    Stats::Gauge::ImportMode::NeverImport, tags_vctr)
      .sub(amount);
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::recordHistogramValue(const std::string& elements, envoy_stats_tags tags,
                                            uint64_t value,
                                            envoy_histogram_stat_unit_t unit_measure) {
  ENVOY_LOG(trace, "[pulse.{}] recordHistogramValue", elements);
  ASSERT(dispatcher_->isThreadSafe(), "pulse calls must run from dispatcher's context");
  Stats::StatNameTagVector tags_vctr =
      Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
  std::string name = Stats::Utility::sanitizeStatsName(elements);
  Stats::Histogram::Unit envoy_unit_measure = Stats::Histogram::Unit::Unspecified;
  switch (unit_measure) {
  case MILLISECONDS:
    envoy_unit_measure = Stats::Histogram::Unit::Milliseconds;
    break;
  case MICROSECONDS:
    envoy_unit_measure = Stats::Histogram::Unit::Microseconds;
    break;
  case BYTES:
    envoy_unit_measure = Stats::Histogram::Unit::Bytes;
    break;
  case UNSPECIFIED:
    envoy_unit_measure = Stats::Histogram::Unit::Unspecified;
    break;
  }

  Stats::Utility::histogramFromElements(*client_scope_, {Stats::DynamicName(name)},
                                        envoy_unit_measure, tags_vctr)
      .recordValue(value);
  return ENVOY_SUCCESS;
}

envoy_status_t Engine::makeAdminCall(absl::string_view path, absl::string_view method,
                                     envoy_data& out) {
  ENVOY_LOG(trace, "admin call {} {}", method, path);

  ASSERT(dispatcher_->isThreadSafe(), "admin calls must be run from the dispatcher's context");
  auto response_headers = Http::ResponseHeaderMapImpl::create();
  std::string body;
  const auto code = server_->admin().request(path, method, *response_headers, body);
  if (code != Http::Code::OK) {
    ENVOY_LOG(warn, "admin call failed with status {} body {}", code, body);
    return ENVOY_FAILURE;
  }

  out = Data::Utility::copyToBridgeData(body);

  return ENVOY_SUCCESS;
}

Event::ProvisionalDispatcher& Engine::dispatcher() { return *dispatcher_; }

Http::Client& Engine::httpClient() {
  RELEASE_ASSERT(dispatcher_->isThreadSafe(),
                 "httpClient must be accessed from dispatcher's context");
  return *http_client_;
}

void Engine::flushStats() {
  ASSERT(dispatcher_->isThreadSafe(), "flushStats must be called from the dispatcher's context");

  server_->flushStats();
}

void Engine::drainConnections() {
  ASSERT(dispatcher_->isThreadSafe(),
         "drainConnections must be called from the dispatcher's context");
  server_->clusterManager().drainConnections();
}

void Engine::logInterfaces() {
  auto v4_vec = Network::MobileUtility::enumerateV4Interfaces();
  std::string v4_names = std::accumulate(v4_vec.begin(), v4_vec.end(), std::string{},
                                         [](std::string acc, std::string next) {
                                           return acc.empty() ? next : std::move(acc) + "," + next;
                                         });

  auto v6_vec = Network::MobileUtility::enumerateV6Interfaces();
  std::string v6_names = std::accumulate(v6_vec.begin(), v6_vec.end(), std::string{},
                                         [](std::string acc, std::string next) {
                                           return acc.empty() ? next : std::move(acc) + "," + next;
                                         });
  ENVOY_LOG_EVENT(debug, "socket_selection_get_v4_interfaces", v4_names);
  ENVOY_LOG_EVENT(debug, "socket_selection_get_v6_interfaces", v6_names);
}

} // namespace Envoy
