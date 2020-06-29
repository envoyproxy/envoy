#pragma once
// NOLINT(namespace-envoy)

/**
 * Benchmarks can use this to skip or hurry through long-running tests in CI.
 */
bool SkipExpensiveBenchmarks();
