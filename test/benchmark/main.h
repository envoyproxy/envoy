#pragma once

/**
 * Benchmarks can use this to skip or hurry through long-running tests in CI.
 */

namespace Envoy {
namespace benchmark {

bool skipExpensiveBenchmarks();

}
} // namespace Envoy
