#include "library/common/main_interface.h"

#include <atomic>
#include <string>

#include "absl/synchronization/notification.h"
#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/engine.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/http/client.h"
#include "library/common/network/connectivity_manager.h"

// NOLINT(namespace-envoy)

namespace {
envoy_status_t runOnEngineDispatcher(envoy_engine_t handle,
                                     std::function<void(Envoy::Engine&)> func) {
  if (auto engine = reinterpret_cast<Envoy::Engine*>(handle)) {
    return engine->dispatcher().post([engine, func]() { func(*engine); });
  }
  return ENVOY_FAILURE;
}
} // namespace

static std::atomic<envoy_stream_t> current_stream_handle_{0};

envoy_stream_t init_stream(envoy_engine_t) { return current_stream_handle_++; }

envoy_status_t start_stream(envoy_engine_t engine, envoy_stream_t stream,
                            envoy_http_callbacks callbacks, bool explicit_flow_control) {
  return runOnEngineDispatcher(
      engine, [stream, callbacks, explicit_flow_control](auto& engine) -> void {
        engine.httpClient().startStream(stream, callbacks, explicit_flow_control);
      });
}

envoy_status_t send_headers(envoy_engine_t engine, envoy_stream_t stream, envoy_headers headers,
                            bool end_stream) {
  return runOnEngineDispatcher(engine, ([stream, headers, end_stream](auto& engine) -> void {
                                 engine.httpClient().sendHeaders(stream, headers, end_stream);
                               }));
}

envoy_status_t read_data(envoy_engine_t engine, envoy_stream_t stream, size_t bytes_to_read) {
  return runOnEngineDispatcher(engine, [stream, bytes_to_read](auto& engine) -> void {
    engine.httpClient().readData(stream, bytes_to_read);
  });
}

envoy_status_t send_data(envoy_engine_t engine, envoy_stream_t stream, envoy_data data,
                         bool end_stream) {
  return runOnEngineDispatcher(engine, [stream, data, end_stream](auto& engine) -> void {
    engine.httpClient().sendData(stream, data, end_stream);
  });
}

// TODO: implement.
envoy_status_t send_metadata(envoy_engine_t, envoy_stream_t, envoy_headers) {
  return ENVOY_FAILURE;
}

envoy_status_t send_trailers(envoy_engine_t engine, envoy_stream_t stream, envoy_headers trailers) {
  return runOnEngineDispatcher(engine, [stream, trailers](auto& engine) -> void {
    engine.httpClient().sendTrailers(stream, trailers);
  });
}

envoy_status_t reset_stream(envoy_engine_t engine, envoy_stream_t stream) {
  return runOnEngineDispatcher(
      engine, [stream](auto& engine) -> void { engine.httpClient().cancelStream(stream); });
}

envoy_status_t set_preferred_network(envoy_engine_t engine, envoy_network_t network) {
  envoy_netconf_t configuration_key =
      Envoy::Network::ConnectivityManagerImpl::setPreferredNetwork(network);
  runOnEngineDispatcher(engine, [configuration_key](auto& engine) -> void {
    engine.networkConnectivityManager().refreshDns(configuration_key, true);
  });
  // TODO(snowp): Should this return failure ever?
  return ENVOY_SUCCESS;
}

envoy_status_t set_proxy_settings(envoy_engine_t e, const char* host, const uint16_t port) {
  return runOnEngineDispatcher(
      e,
      [proxy_settings = Envoy::Network::ProxySettings::parseHostAndPort(host, port)](auto& engine)
          -> void { engine.networkConnectivityManager().setProxySettings(proxy_settings); });
}

envoy_status_t record_counter_inc(envoy_engine_t e, const char* elements, envoy_stats_tags tags,
                                  uint64_t count) {
  return runOnEngineDispatcher(e,
                               [name = std::string(elements), tags, count](auto& engine) -> void {
                                 engine.recordCounterInc(name, tags, count);
                               });
}

envoy_status_t dump_stats(envoy_engine_t engine, envoy_data* out) {
  absl::Notification stats_received;
  if (runOnEngineDispatcher(engine, ([out, &stats_received](auto& engine) -> void {
                              Envoy::Buffer::OwnedImpl dumped_stats = engine.dumpStats();
                              *out = Envoy::Data::Utility::toBridgeData(dumped_stats,
                                                                        1024 * 1024 * 100);
                              stats_received.Notify();
                            })) == ENVOY_FAILURE) {
    return ENVOY_FAILURE;
  }
  stats_received.WaitForNotification();
  return ENVOY_SUCCESS;
}

envoy_status_t register_platform_api(const char* name, void* api) {
  Envoy::Api::External::registerApi(std::string(name), api);
  return ENVOY_SUCCESS;
}

envoy_engine_t init_engine(envoy_engine_callbacks callbacks, envoy_logger logger,
                           envoy_event_tracker event_tracker) {
  auto engine = new Envoy::Engine(callbacks, logger, event_tracker);
  return reinterpret_cast<envoy_engine_t>(engine);
}

envoy_status_t run_engine(envoy_engine_t handle, const char* config, const char* log_level) {
  if (auto engine = reinterpret_cast<Envoy::Engine*>(handle)) {
    engine->run(config, log_level);
    return ENVOY_SUCCESS;
  }
  return ENVOY_FAILURE;
}

envoy_status_t terminate_engine(envoy_engine_t handle, bool release) {
  auto engine = reinterpret_cast<Envoy::Engine*>(handle);
  envoy_status_t ret = engine->terminate();
  if (release) {
    // TODO(jpsim): Always delete engine to avoid leaking it
    delete engine;
  }
  return ret;
}

envoy_status_t reset_connectivity_state(envoy_engine_t e) {
  return runOnEngineDispatcher(
      e, [](auto& engine) { engine.networkConnectivityManager().resetConnectivityState(); });
}
