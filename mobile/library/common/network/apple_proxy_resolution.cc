#include "library/common/network/apple_proxy_resolution.h"

#include <string>

#include "library/common/main_interface.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

envoy_data toManagedNativeString(std::string str) {
  size_t length = str.length();
  uint8_t* native_string = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * length));
  memcpy(native_string, str.c_str(), length); // NOLINT(safe-memcpy)
  envoy_data ret = {length, native_string, free, native_string};
  return ret;
}

static inline envoy_proxy_settings_list
toNativeEnvoyProxySettingsList(std::vector<ProxySettings> proxies) {
  envoy_proxy_settings_list list;
  list.length = static_cast<uint64_t>(proxies.size());
  list.proxy_settings =
      static_cast<envoy_proxy_settings*>(safe_malloc(sizeof(envoy_proxy_settings) * list.length));

  for (unsigned long i = 0; i < proxies.size(); i++) {
    ProxySettings current = proxies[i];
    envoy_proxy_type type =
        current.isDirect() ? ENVOY_PROXY_TYPE_DIRECT : ENVOY_PROXY_TYPE_HTTPS_OR_HTTP;

    envoy_proxy_settings proxy_settings = {
        .host_data = toManagedNativeString(current.hostname()),
        .port = current.port(),
        .type = type,
    };

    list.proxy_settings[i] = proxy_settings;
  }

  return list;
}

envoy_proxy_resolution_result
apple_resolve_proxy(envoy_data c_host, envoy_proxy_settings_list* proxy_settings_list,
                    const envoy_proxy_resolver_proxy_resolution_result_handler* result_handler,
                    void* raw_context) {

  envoy_proxy_resolver_context* context = static_cast<envoy_proxy_resolver_context*>(raw_context);

  std::string hostname(reinterpret_cast<const char*>(c_host.bytes), c_host.length);
  release_envoy_data(c_host);

  std::vector<ProxySettings> proxies;
  auto result = context->resolver->resolveProxy(
      hostname, proxies, [context, result_handler](std::vector<ProxySettings> proxies) {
        envoy_proxy_settings_list envoy_proxies = toNativeEnvoyProxySettingsList(proxies);
        complete_proxy_resolution(context->engine_handle, envoy_proxies, result_handler);
      });
  if (result == ENVOY_PROXY_RESOLUTION_RESULT_COMPLETED) {
    *proxy_settings_list = toNativeEnvoyProxySettingsList(proxies);
  }

  return result;
}

} // namespace Network
} // namespace Envoy

#ifdef __cplusplus
extern "C" {
#endif

void register_apple_proxy_resolver(envoy_engine_t engine_handle) {
  envoy_proxy_resolver* envoy_resolver =
      static_cast<envoy_proxy_resolver*>(safe_malloc(sizeof(envoy_proxy_resolver)));

  auto resolver = std::make_unique<Envoy::Network::AppleProxyResolver>();
  resolver->start();

  auto envoy_proxy_resolver_context = static_cast<Envoy::Network::envoy_proxy_resolver_context*>(
      safe_malloc(sizeof(Envoy::Network::envoy_proxy_resolver_context)));
  envoy_proxy_resolver_context->engine_handle = engine_handle;
  envoy_proxy_resolver_context->resolver = std::move(resolver);

  envoy_resolver->context = envoy_proxy_resolver_context;
  envoy_resolver->resolve = Envoy::Network::apple_resolve_proxy;

  register_platform_api("envoy_proxy_resolver", envoy_resolver);
}

#ifdef __cplusplus
}
#endif
