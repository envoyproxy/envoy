#include "library/common/network/apple_proxy_resolution.h"

#include <memory>

#include "library/common/api/external.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/network/proxy_api.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

void registerAppleProxyResolver() {
  auto resolver = std::make_unique<Envoy::Network::AppleProxyResolver>();
  resolver->start();

  auto api = std::make_unique<Envoy::Network::ProxyResolverApi>();
  api->resolver = std::move(resolver);

  Envoy::Api::External::registerApi("envoy_proxy_resolver", api.release());
}

#ifdef __cplusplus
}
#endif
