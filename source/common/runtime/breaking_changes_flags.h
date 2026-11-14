#pragma once

// NOLINT(namespace-envoy)

// List of all breaking changes tracked for observability.
//
// Breaking changes are changes that may have an observable impact on existing production traffic.
// Tracking them allows operators to monitor their occurrence via filter state and access logs
// before old behaviors are removed.
//
// Each entry takes either:
// - ENABLED(name): for breaking changes enabled by default.
// - DISABLED(name): for breaking changes disabled by default.
//
// This macro list is used to declare corresponding runtime feature flags, generate bitfield
// members in BreakingChangesTracker, and serialize observed breaking changes into access logs.
#define ALL_BREAKING_CHANGES(ENABLED, DISABLED)                                                    \
  /* Always present test flags for testing the breaking changes library. */                        \
  ENABLED(test_enabled_breaking_change)                                                            \
  DISABLED(test_disabled_breaking_change)
