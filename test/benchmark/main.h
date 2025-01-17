#pragma once

#include <functional>

/**
 * Benchmarks can use this to skip or hurry through long-running tests in CI.
 */

namespace Envoy {
namespace benchmark {

bool skipExpensiveBenchmarks();

/**
 * Establishes a function to run before the test process exits. This enables
 * threads, mocks, and other objects that are expensive to create to be shared
 * between test methods.
 */
void setCleanupHook(std::function<void()>);

} // namespace benchmark
} // namespace Envoy
