#include "library/common/network/apple_proxy_resolution.h"

#include <string>

#include "library/common/main_interface.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/network/proxy_settings.h"

envoy_proxy_resolution_result
apple_resolve_proxy(envoy_data c_host, envoy_proxy_settings_list* proxy_settings_list,
                    const envoy_proxy_resolver_proxy_resolution_result_handler* result_handler,
                    const void* context) {

  AppleProxyResolver* resolver = static_cast<AppleProxyResolver*>(context);

  std::string hostname(c_host.bytes, c_host.length);
  release_envoy_data(c_host);

  std::vector<ProxySettings> proxies;
  auto result = resolver->resolveProxy(hostname, proxies&, [](std::vector<ProxySettings> proxies){
    envoy_proxy_settings_list proxyList = toNativeEnvoyProxySettingsList(proxies);
    complete_proxy_resolution(resolutionContext.engineHandle, proxyList, result_handler);
  });
  if (result == ENVOY_PROXY_RESOLUTION_RESULT_COMPLETED) {
    *proxy_settings_list = toNativeEnvoyProxySettingsList(proxies)
  }

  return result;

//  NSString* host = to_ios_string(c_host);
//  NSArray<EnvoyProxySettings*>* proxySettings;

//  envoy_proxy_resolution_result result = [resolutionContext.proxyResolver
//      resolveProxyForTargetURL:[NSURL URLWithString:host]
//                 proxySettings:&proxySettings
//           withCompletionBlock:^(NSArray<EnvoyProxySettings*>* _Nullable settings,
//                                 NSError* _Nullable error) {
//             } else {
//    envoy_proxy_settings_list proxyList = toNativeEnvoyProxySettingsList(settings);
//    complete_proxy_resolution(resolutionContext.engineHandle, proxyList, result_handler);
//             }
//}];
//
//if (result == ENVOY_PROXY_RESOLUTION_RESULT_COMPLETED) {
//  *proxy_settings_list = toNativeEnvoyProxySettingsList(proxySettings);
//}
//
//return result;
}

static inline envoy_proxy_settings_list toNativeEnvoyProxySettingsList(std::vector<ProxySettings> proxies) {
  envoy_proxy_settings_list list;
  list.length = (uint64_t)proxySettingsList.count;
  list.proxy_settings =
      (envoy_proxy_settings *)safe_malloc(sizeof(envoy_proxy_settings) * list.length);

  for (int i = 0; i < proxySettingsList.count; i++) {
    ProxySettings current = proxies[i];
//    envoy_proxy_type type =
//        current.isDirect ? ENVOY_PROXY_TYPE_DIRECT : ENVOY_PROXY_TYPE_HTTPS_OR_HTTP;
//
//    envoy_proxy_settings proxy_settings = {
//        .host_data = toManagedNativeString(proxySettingsList[i].host),
//        .port = proxySettingsList[i].port,
//        .type = type,
//    };
//
//    list.proxy_settings[i] = proxy_settings;
  }

  return list;
}

#ifdef __cplusplus
extern "C" {
#endif

void register_apple_platform_cert_verifier(envoy_engine_t engine_handle) {
  envoy_proxy_resolver* resolver = (envoy_proxy_resolver*)safe_malloc(sizeof(envoy_proxy_resolver));

  auto resolver = new AppleProxyResolver();
  resolver->start();

  resolver->context = resolver;
  resolver->resolve = apple_resolve_proxy;

  register_platform_api("envoy_proxy_resolver", resolver);
}

#ifdef __cplusplus
}
#endif
