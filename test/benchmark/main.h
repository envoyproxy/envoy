#pragma once

#include <functional>

/**
 * Benchmarks can use this to skip or hurry through long-running tests in CI.
 */

namespace Envoy {
namespace benchmark {

bool skipExpensiveBenchmarks();
//void setStartupHook(std::function<void()>);
void setCleanupHook(std::function<void()>);

}
} // namespace Envoy
