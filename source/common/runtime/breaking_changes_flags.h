#pragma once

// NOLINT(namespace-envoy)

#define ALL_BREAKING_CHANGES(ENABLED, DISABLED)                                                    \
  ENABLED(test_enabled_breaking_change)                                                            \
  DISABLED(test_disabled_breaking_change)
