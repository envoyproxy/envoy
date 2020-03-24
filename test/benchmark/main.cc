// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.

#include "benchmark/benchmark.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif

#include "absl/debugging/symbolize.h"

// Boilerplate main(), which discovers benchmarks and runs them.
int main(int argc, char** argv) {
#ifndef __APPLE__
  absl::InitializeSymbolizer(argv[0]);
#endif
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
