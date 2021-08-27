#include "library/common/main_interface.h"

#include <atomic>
#include <string>

#include "absl/synchronization/notification.h"
#include "library/common/api/external.h"
#include "library/common/engine.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/http/client.h"
#include "types/c_types.h"

// NOLINT(namespace-envoy)

static Envoy::EngineSharedPtr strong_engine_;
static Envoy::EngineWeakPtr engine_;
static std::atomic<envoy_stream_t> current_stream_handle_{0};
static std::atomic<envoy_network_t> preferred_network_{ENVOY_NET_GENERIC};

static std::shared_ptr<Envoy::Engine> engine() {
  // TODO(goaway): enable configurable heap-based allocation
  return engine_.lock();
}

envoy_stream_t init_stream(envoy_engine_t) { return current_stream_handle_++; }

envoy_status_t start_stream(envoy_stream_t stream, envoy_http_callbacks callbacks,
                            bool explicit_flow_control) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream, callbacks, explicit_flow_control]() -> void {
      if (auto e = engine())
        e->httpClient().startStream(stream, callbacks, explicit_flow_control);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t send_headers(envoy_stream_t stream, envoy_headers headers, bool end_stream) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream, headers, end_stream]() -> void {
      if (auto e = engine())
        e->httpClient().sendHeaders(stream, headers, end_stream);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t read_data(envoy_stream_t stream, size_t bytes_to_read) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream, bytes_to_read]() -> void {
      if (auto e = engine())
        e->httpClient().readData(stream, bytes_to_read);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t send_data(envoy_stream_t stream, envoy_data data, bool end_stream) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream, data, end_stream]() -> void {
      if (auto e = engine())
        e->httpClient().sendData(stream, data, end_stream);
    });
  }
  return ENVOY_FAILURE;
}

// TODO: implement.
envoy_status_t send_metadata(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }

envoy_status_t send_trailers(envoy_stream_t stream, envoy_headers trailers) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream, trailers]() -> void {
      if (auto e = engine())
        e->httpClient().sendTrailers(stream, trailers);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t reset_stream(envoy_stream_t stream) {
  if (auto e = engine()) {
    return e->dispatcher().post([stream]() -> void {
      if (auto e = engine())
        e->httpClient().cancelStream(stream);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t set_preferred_network(envoy_network_t network) {
  preferred_network_.store(network);
  return ENVOY_SUCCESS;
}

envoy_status_t record_counter_inc(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                  uint64_t count) {
  // TODO: use specific engine once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    return e->dispatcher().post([name = std::string(elements), tags, count]() -> void {
      if (auto e = engine())
        e->recordCounterInc(name, tags, count);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t record_gauge_set(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                uint64_t value) {
  // TODO: use specific engine once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    return e->dispatcher().post([name = std::string(elements), tags, value]() -> void {
      if (auto e = engine())
        e->recordGaugeSet(name, tags, value);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t record_gauge_add(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                uint64_t amount) {
  // TODO: use specific engine once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    return e->dispatcher().post([name = std::string(elements), tags, amount]() -> void {
      if (auto e = engine())
        e->recordGaugeAdd(name, tags, amount);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t record_gauge_sub(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                uint64_t amount) {
  // TODO: use specific engine once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    return e->dispatcher().post([name = std::string(elements), tags, amount]() -> void {
      if (auto e = engine())
        e->recordGaugeSub(name, tags, amount);
    });
  }
  return ENVOY_FAILURE;
}

envoy_status_t record_histogram_value(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                      uint64_t value, envoy_histogram_stat_unit_t unit_measure) {
  // TODO: use specific engine once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    return e->dispatcher().post(
        [name = std::string(elements), tags, value, unit_measure]() -> void {
          if (auto e = engine())
            e->recordHistogramValue(name, tags, value, unit_measure);
        });
  }
  return ENVOY_FAILURE;
}

namespace {
struct AdminCallContext {
  envoy_status_t status_{};
  envoy_data response_{};

  absl::Mutex mutex_{};
  absl::Notification data_received_{};
};

absl::optional<envoy_data> blockingAdminCall(absl::string_view path, absl::string_view method,
                                             std::chrono::milliseconds timeout) {
  if (auto e = engine()) {
    // Use a shared ptr here so that we can safely exit this scope in case of a timeout,
    // allowing the dispatched lambda to clean itself up when it's done.
    auto context = std::make_shared<AdminCallContext>();
    const auto status = e->dispatcher().post(
        [context, path = std::string(path), method = std::string(method)]() -> void {
          if (auto e = engine()) {
            absl::MutexLock lock(&context->mutex_);

            context->status_ = e->makeAdminCall(path, method, context->response_);
            context->data_received_.Notify();
          }
        });

    if (status == ENVOY_FAILURE) {
      return {};
    }

    if (context->data_received_.WaitForNotificationWithTimeout(
            absl::Milliseconds(timeout.count()))) {
      absl::MutexLock lock(&context->mutex_);

      if (context->status_ == ENVOY_FAILURE) {
        return {};
      }

      return context->response_;
    } else {
      ENVOY_LOG_MISC(warn, "timed out waiting for admin response");
    }
  }

  return {};
}
} // namespace

envoy_status_t dump_stats(envoy_engine_t, envoy_data* out) {
  auto maybe_data = blockingAdminCall("/stats?usedonly", "GET", std::chrono::milliseconds(100));
  if (maybe_data) {
    *out = *maybe_data;
    return ENVOY_SUCCESS;
  }

  return ENVOY_FAILURE;
}

void flush_stats(envoy_engine_t) {
  if (auto e = engine()) {
    e->dispatcher().post([]() {
      if (auto e = engine()) {
        e->flushStats();
      }
    });
  }
}

envoy_status_t register_platform_api(const char* name, void* api) {
  Envoy::Api::External::registerApi(std::string(name), api);
  return ENVOY_SUCCESS;
}

envoy_engine_t init_engine(envoy_engine_callbacks callbacks, envoy_logger logger,
                           envoy_event_tracker event_tracker) {
  // TODO(goaway): return new handle once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  strong_engine_ =
      std::make_shared<Envoy::Engine>(callbacks, logger, event_tracker, preferred_network_);
  engine_ = strong_engine_;
  return 1;
}

envoy_status_t run_engine(envoy_engine_t, const char* config, const char* log_level) {
  // This will change once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    e->run(config, log_level);
    return ENVOY_SUCCESS;
  }

  return ENVOY_FAILURE;
}

void terminate_engine(envoy_engine_t) {
  // Reset the primary handle to the engine, but retain it long enough to synchronously terminate.
  auto e = strong_engine_;
  strong_engine_.reset();
  e->terminate();
}

envoy_status_t drain_connections(envoy_engine_t) {
  // This will change once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  if (auto e = engine()) {
    e->dispatcher().post([]() {
      if (auto e = engine()) {
        e->drainConnections();
      }
    });
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}
