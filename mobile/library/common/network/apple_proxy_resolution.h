#pragma once

#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Network {

typedef struct {
  std::unique_ptr<Envoy::Network::AppleProxyResolver> resolver;
  envoy_engine_t engine_handle;
} envoy_proxy_resolver_context;

} // namespace Network
} // namespace Envoy

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registers the Apple proxy resolver.
 */
void register_apple_proxy_resolver(envoy_engine_t engine_handle);

#ifdef __cplusplus
}
#endif
