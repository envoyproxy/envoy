// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.
#include "test/test_common/environment.h"

#include "benchmark/benchmark.h"

// Boilerplate main(), which discovers benchmarks and runs them.
int main(int argc, char** argv) {
  Envoy::TestEnvironment::initializeTestMain(argv[0]);

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
