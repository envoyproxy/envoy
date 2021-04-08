#include "library/common/engine.h"

#include "envoy/stats/histogram.h"

#include "common/common/lock_guard.h"

#include "library/common/data/utility.h"
#include "library/common/stats/utility.h"

namespace Envoy {

Engine::Engine(envoy_engine_callbacks callbacks, envoy_logger, const char* config,
               const char* log_level, std::atomic<envoy_network_t>& preferred_network)
    : callbacks_(callbacks) {
  // Ensure static factory registration occurs on time.
  // TODO: ensure this is only called one time once multiple Engine objects can be allocated.
  // https://github.com/lyft/envoy-mobile/issues/332
  ExtensionRegistry::registerFactories();

  // Create the Http::Dispatcher first since it contains initial queueing logic.
  // TODO: consider centralizing initial queueing in this class.
  // https://github.com/lyft/envoy-mobile/issues/720
  http_dispatcher_ = std::make_unique<Http::Dispatcher>(preferred_network);

  // Start the Envoy on a dedicated thread.
  main_thread_ = std::thread(&Engine::run, this, std::string(config), std::string(log_level));
}

envoy_status_t Engine::run(const std::string config, const std::string log_level) {
  {
    Thread::LockGuard lock(mutex_);
    try {
      const std::string name = "envoy";
      const std::string config_flag = "--config-yaml";
      const std::string log_flag = "-l";
      const std::string concurrency_option = "--concurrency";
      const std::string concurrency_arg = "0";
      std::vector<const char*> envoy_argv = {name.c_str(),
                                             config_flag.c_str(),
                                             config.c_str(),
                                             concurrency_option.c_str(),
                                             concurrency_arg.c_str(),
                                             log_flag.c_str(),
                                             log_level.c_str(),
                                             nullptr};
      main_common_ = std::make_unique<MobileMainCommon>(envoy_argv.size() - 1, envoy_argv.data());

      event_dispatcher_ = &main_common_->server()->dispatcher();

      // TODO(junr03): wire up after https://github.com/envoyproxy/envoy-mobile/pull/1354 merges.
      // Logger::LambdaDelegate::LogCb log_cb = [](absl::string_view) -> void {};
      // if (logger_.log) {
      //   log_cb = [this](absl::string_view msg) -> void {
      //     logger_.log(Data::Utility::copyToBridgeData(msg),
      //                               logger_.context);
      //   };
      // }

      // lambda_logger_ =
      //     std::make_unique<Logger::LambdaDelegate>(log_cb, Logger::Registry::getSink());

      cv_.notifyAll();
    } catch (const Envoy::NoServingException& e) {
      std::cerr << e.what() << std::endl;
      return ENVOY_FAILURE;
    } catch (const Envoy::MalformedArgvException& e) {
      std::cerr << e.what() << std::endl;
      return ENVOY_FAILURE;
    } catch (const Envoy::EnvoyException& e) {
      std::cerr << e.what() << std::endl;
      return ENVOY_FAILURE;
    }

    // Note: We're waiting longer than we might otherwise to drain to the main thread's dispatcher.
    // This is because we're not simply waiting for its availability and for it to have started, but
    // also because we're waiting for clusters to have done their first attempt at DNS resolution.
    // When we improve synchronous failure handling and/or move to dynamic forwarding, we only need
    // to wait until the dispatcher is running (and can drain by enqueueing a drain callback on it,
    // as we did previously).
    postinit_callback_handler_ = main_common_->server()->lifecycleNotifier().registerCallback(
        Envoy::Server::ServerLifecycleNotifier::Stage::PostInit, [this]() -> void {
          server_ = TS_UNCHECKED_READ(main_common_)->server();
          client_scope_ = server_->serverFactoryContext().scope().createScope("pulse.");
          // StatNameSet is lock-free, the benefit of using it is being able to create StatsName
          // on-the-fly without risking contention on system with lots of threads.
          // It also comes with ease of programming.
          stat_name_set_ = client_scope_->symbolTable().makeSet("pulse");
          auto api_listener = server_->listenerManager().apiListener()->get().http();
          ASSERT(api_listener.has_value());
          http_dispatcher_->ready(server_->dispatcher(), server_->serverFactoryContext().scope(),
                                  api_listener.value());
          if (callbacks_.on_engine_running != nullptr) {
            callbacks_.on_engine_running(callbacks_.context);
          }
        });
  } // mutex_

  // The main run loop must run without holding the mutex, so that the destructor can acquire it.
  bool run_success = TS_UNCHECKED_READ(main_common_)->run();
  // The above call is blocking; at this point the event loop has exited.

  // Ensure destructors run on Envoy's main thread.
  postinit_callback_handler_.reset(nullptr);
  client_scope_.reset(nullptr);
  stat_name_set_.reset();
  TS_UNCHECKED_READ(main_common_).reset(nullptr);

  callbacks_.on_exit(callbacks_.context);

  return run_success ? ENVOY_SUCCESS : ENVOY_FAILURE;
}

Engine::~Engine() {
  // If we're already on the main thread, it should be safe to simply destruct.
  if (!main_thread_.joinable()) {
    return;
  }

  // If we're not on the main thread, we need to be sure that MainCommon is finished being
  // constructed so we can dispatch shutdown.
  {
    Thread::LockGuard lock(mutex_);

    if (!main_common_) {
      cv_.wait(mutex_);
    }

    ASSERT(main_common_);

    // Exit the event loop and finish up in Engine::run(...)
    event_dispatcher_->exit();
  } // _mutex

  // Now we wait for the main thread to wrap things up.
  main_thread_.join();
}

envoy_status_t Engine::recordCounterInc(const std::string& elements, envoy_stats_tags tags,
                                        uint64_t count) {
  if (server_ && client_scope_ && stat_name_set_) {
    Stats::StatNameTagVector tags_vctr =
        Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
    std::string name = Stats::Utility::sanitizeStatsName(elements);
    server_->dispatcher().post([this, name, tags_vctr, count]() -> void {
      Stats::Utility::counterFromElements(*client_scope_, {Stats::DynamicName(name)}, tags_vctr)
          .add(count);
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

envoy_status_t Engine::recordGaugeSet(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t value) {
  if (server_ && client_scope_ && stat_name_set_) {
    Stats::StatNameTagVector tags_vctr =
        Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
    std::string name = Stats::Utility::sanitizeStatsName(elements);
    server_->dispatcher().post([this, name, tags_vctr, value]() -> void {
      Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                        Stats::Gauge::ImportMode::NeverImport, tags_vctr)
          .set(value);
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

envoy_status_t Engine::recordGaugeAdd(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t amount) {
  if (server_ && client_scope_ && stat_name_set_) {
    Stats::StatNameTagVector tags_vctr =
        Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
    std::string name = Stats::Utility::sanitizeStatsName(elements);
    server_->dispatcher().post([this, name, tags_vctr, amount]() -> void {
      Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                        Stats::Gauge::ImportMode::NeverImport, tags_vctr)
          .add(amount);
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

envoy_status_t Engine::recordGaugeSub(const std::string& elements, envoy_stats_tags tags,
                                      uint64_t amount) {
  if (server_ && client_scope_ && stat_name_set_) {
    Stats::StatNameTagVector tags_vctr =
        Stats::Utility::transformToStatNameTagVector(tags, stat_name_set_);
    std::string name = Stats::Utility::sanitizeStatsName(elements);
    server_->dispatcher().post([this, name, tags_vctr, amount]() -> void {
      Stats::Utility::gaugeFromElements(*client_scope_, {Stats::DynamicName(name)},
                                        Stats::Gauge::ImportMode::NeverImport, tags_vctr)
          .sub(amount);
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

envoy_status_t Engine::recordHistogramValue(const std::string& elements, envoy_stats_tags tags,
                                            uint64_t value,
                                            envoy_histogram_stat_unit_t unit_measure) {
  if (server_ && client_scope_ && stat_name_set_) {
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

    server_->dispatcher().post([this, name, tags_vctr, value, envoy_unit_measure]() -> void {
      Stats::Utility::histogramFromElements(*client_scope_, {Stats::DynamicName(name)},
                                            envoy_unit_measure, tags_vctr)
          .recordValue(value);
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

Http::Dispatcher& Engine::httpDispatcher() { return *http_dispatcher_; }

} // namespace Envoy
