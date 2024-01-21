#pragma once

#include "absl/strings/string_view.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Registers the test Apple proxy resolver. Assumes a non-test proxy resolver is already
 * registered. The non-test proxy resolver gets replaced by the test resolver.
 */
void register_test_apple_proxy_resolver(absl::string_view host, int port);

#ifdef __cplusplus
}
#endif
