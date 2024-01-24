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

envoy_status_t reset_connectivity_state(envoy_engine_t e) {
  return runOnEngineDispatcher(
      e, [](auto& engine) { engine.networkConnectivityManager().resetConnectivityState(); });
}
