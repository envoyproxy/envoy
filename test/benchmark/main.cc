// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.
#include "test/test_common/environment.h"

#include "benchmark/benchmark.h"
#include "tclap/CmdLine.h"

bool g_skip_expensive_benchmarks = false;

// Boilerplate main(), which discovers benchmarks and runs them. This uses two
// different flag parsers, so the order of flags matters: flags defined here
// must be passed first, and flags defined in benchmark::Initialize second,
// seperated by --.
int main(int argc, char** argv) {
  Envoy::TestEnvironment::initializeTestMain(argv[0]);

  TCLAP::CmdLine cmd("envoy-benchmark-test", ' ', "0.1");
  TCLAP::SwitchArg skip_switch("s", "skip_expensive_benchmarks",
                               "skip or minimize expensive benchmarks", cmd, false);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
  } catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output
    // for --help and --version.
    return 0;
  }

  g_skip_expensive_benchmarks = skip_switch.getValue();

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
}
