#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registers the Apple proxy resolver.
 */
void registerAppleProxyResolver(int refresh_interval_secs);

#ifdef __cplusplus
}
#endif
