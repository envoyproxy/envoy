#pragma once

#include "absl/strings/string_view.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registers the test Apple proxy resolver. If the `envoy_proxy_resolver` API already exists, it
 * gets removed and deleted first. The test proxy resolver then gets registered as the
 * `envoy_proxy_resolver` API.
 *
 * @param host The hostname of the test proxy.
 * @param port The port of the test proxy.
 */
void register_test_apple_proxy_resolver(absl::string_view host, int port);

#ifdef __cplusplus
}
#endif
