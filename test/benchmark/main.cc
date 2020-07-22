// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.
#include "test/benchmark/main.h"

#include "common/common/logger.h"

#include "test/test_common/environment.h"

#include "benchmark/benchmark.h"
#include "tclap/CmdLine.h"

using namespace Envoy;

static bool skip_expensive_benchmarks = false;

// Boilerplate main(), which discovers benchmarks and runs them. This uses two
// different flag parsers, so the order of flags matters: flags defined here
// must be passed first, and flags defined in benchmark::Initialize second,
// separated by --.
// TODO(pgenera): convert this to abseil/flags/ when benchmark also adopts abseil.
int main(int argc, char** argv) {
  TestEnvironment::initializeTestMain(argv[0]);

  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
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

  skip_expensive_benchmarks = skip_switch.getValue();

  ::benchmark::Initialize(&argc, argv);

  if (skip_expensive_benchmarks) {
    ENVOY_LOG_MISC(
        critical,
        "Expensive benchmarks are being skipped; see test/README.md for more information");
  }
  ::benchmark::RunSpecifiedBenchmarks();
}

bool Envoy::benchmark::skipExpensiveBenchmarks() { return skip_expensive_benchmarks; }
