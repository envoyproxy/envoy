#include "library/common/main_interface.h"

#include <atomic>
#include <string>

#include "absl/synchronization/notification.h"
#include "library/common/api/external.h"
#include "library/common/engine.h"
#include "library/common/engine_handle.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/http/client.h"
#include "library/common/network/connectivity_manager.h"

// NOLINT(namespace-envoy)

static std::atomic<envoy_stream_t> current_stream_handle_{0};

envoy_stream_t init_stream(envoy_engine_t) { return current_stream_handle_++; }

envoy_status_t start_stream(envoy_engine_t engine, envoy_stream_t stream,
                            envoy_http_callbacks callbacks, bool explicit_flow_control) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, [stream, callbacks, explicit_flow_control](auto& engine) -> void {
        engine.httpClient().startStream(stream, callbacks, explicit_flow_control);
      });
}

envoy_status_t send_headers(envoy_engine_t engine, envoy_stream_t stream, envoy_headers headers,
                            bool end_stream) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, ([stream, headers, end_stream](auto& engine) -> void {
        engine.httpClient().sendHeaders(stream, headers, end_stream);
      }));
}

envoy_status_t read_data(envoy_engine_t engine, envoy_stream_t stream, size_t bytes_to_read) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, [stream, bytes_to_read](auto& engine) -> void {
        engine.httpClient().readData(stream, bytes_to_read);
      });
}

envoy_status_t send_data(envoy_engine_t engine, envoy_stream_t stream, envoy_data data,
                         bool end_stream) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, [stream, data, end_stream](auto& engine) -> void {
        engine.httpClient().sendData(stream, data, end_stream);
      });
}

// TODO: implement.
envoy_status_t send_metadata(envoy_engine_t, envoy_stream_t, envoy_headers) {
  return ENVOY_FAILURE;
}

envoy_status_t send_trailers(envoy_engine_t engine, envoy_stream_t stream, envoy_headers trailers) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, [stream, trailers](auto& engine) -> void {
        engine.httpClient().sendTrailers(stream, trailers);
      });
}

envoy_status_t reset_stream(envoy_engine_t engine, envoy_stream_t stream) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      engine, [stream](auto& engine) -> void { engine.httpClient().cancelStream(stream); });
}

envoy_status_t set_preferred_network(envoy_engine_t engine, envoy_network_t network) {
  envoy_netconf_t configuration_key =
      Envoy::Network::ConnectivityManager::setPreferredNetwork(network);
  Envoy::EngineHandle::runOnEngineDispatcher(engine, [configuration_key](auto& engine) -> void {
    engine.networkConnectivityManager().refreshDns(configuration_key, true);
  });
  // TODO(snowp): Should this return failure ever?
  return ENVOY_SUCCESS;
}

envoy_status_t record_counter_inc(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                  uint64_t count) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [name = std::string(elements), tags, count](auto& engine) -> void {
        engine.recordCounterInc(name, tags, count);
      });
}

envoy_status_t record_gauge_set(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                uint64_t value) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [name = std::string(elements), tags, value](auto& engine) -> void {
        engine.recordGaugeSet(name, tags, value);
      });
}

envoy_status_t record_gauge_add(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                uint64_t amount) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [name = std::string(elements), tags, amount](auto& engine) -> void {
        engine.recordGaugeAdd(name, tags, amount);
      });
}

envoy_status_t record_gauge_sub(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                uint64_t amount) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [name = std::string(elements), tags, amount](auto& engine) -> void {
        engine.recordGaugeSub(name, tags, amount);
      });
}

envoy_status_t record_histogram_value(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                      uint64_t value, envoy_histogram_stat_unit_t unit_measure) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [name = std::string(elements), tags, value, unit_measure](auto& engine) -> void {
        engine.recordHistogramValue(name, tags, value, unit_measure);
      });
}

namespace {
struct AdminCallContext {
  envoy_status_t status_{};
  envoy_data response_{};

  absl::Mutex mutex_{};
  absl::Notification data_received_{};
};

absl::optional<envoy_data> blockingAdminCall(envoy_engine_t e, absl::string_view path,
                                             absl::string_view method,
                                             std::chrono::milliseconds timeout) {
  // Use a shared ptr here so that we can safely exit this scope in case of a timeout,
  // allowing the dispatched lambda to clean itself up when it's done.
  auto context = std::make_shared<AdminCallContext>();

  auto status = Envoy::EngineHandle::runOnEngineDispatcher(
      e, [context, path = std::string(path), method = std::string(method)](auto& engine) -> void {
        absl::MutexLock lock(&context->mutex_);

        context->status_ = engine.makeAdminCall(path, method, context->response_);
        context->data_received_.Notify();
      });

  if (status == ENVOY_FAILURE) {
    return {};
  }

  if (context->data_received_.WaitForNotificationWithTimeout(absl::Milliseconds(timeout.count()))) {
    absl::MutexLock lock(&context->mutex_);

    if (context->status_ == ENVOY_FAILURE) {
      return {};
    }

    return context->response_;
  } else {
    ENVOY_LOG_MISC(warn, "timed out waiting for admin response");
  }

  return {};
}
} // namespace

envoy_status_t dump_stats(envoy_engine_t e, envoy_data* out) {
  auto maybe_data = blockingAdminCall(e, "/stats?usedonly", "GET", std::chrono::milliseconds(100));
  if (maybe_data) {
    *out = *maybe_data;
    return ENVOY_SUCCESS;
  }

  return ENVOY_FAILURE;
}

void flush_stats(envoy_engine_t e) {
  Envoy::EngineHandle::runOnEngineDispatcher(e, [](auto& engine) { engine.flushStats(); });
}

envoy_status_t register_platform_api(const char* name, void* api) {
  Envoy::Api::External::registerApi(std::string(name), api);
  return ENVOY_SUCCESS;
}

envoy_engine_t init_engine(envoy_engine_callbacks callbacks, envoy_logger logger,
                           envoy_event_tracker event_tracker) {
  return Envoy::EngineHandle::initEngine(callbacks, logger, event_tracker);
}

envoy_status_t run_engine(envoy_engine_t engine, const char* config, const char* log_level,
                          const char* admin_path) {
  return Envoy::EngineHandle::runEngine(engine, config, log_level, admin_path);
}

void terminate_engine(envoy_engine_t engine, bool release) {
  Envoy::EngineHandle::terminateEngine(engine, release);
}

envoy_status_t reset_connectivity_state(envoy_engine_t e) {
  return Envoy::EngineHandle::runOnEngineDispatcher(
      e, [](auto& engine) { engine.networkConnectivityManager().resetConnectivityState(); });
}
