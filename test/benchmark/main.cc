// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.
#include "test/benchmark/main.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"

#include "benchmark/benchmark.h"
#include "tclap/CmdLine.h"

using namespace Envoy;

static bool skip_expensive_benchmarks = false;
static std::function<void()> cleanup_hook = []() {};

// Boilerplate main(), which discovers benchmarks and runs them. This uses two
// different flag parsers, so the order of flags matters: flags defined here
// must be passed first, and flags defined in benchmark::Initialize second,
// separated by --.
// TODO(pgenera): convert this to abseil/flags/ when benchmark also adopts abseil.
int main(int argc, char** argv) {

  bool contains_help_flag = false;

  // Checking if any of the command-line arguments contains `--help`
  for (int i = 1; i < argc; ++i) { // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
    if (strcmp(argv[i], "--help") == 0) {
      contains_help_flag = true;
      break;
    }
  }

  if (contains_help_flag) { // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)
    // if the `--help` flag isn't considered separately, it runs "benchmark --help"
    // (Google Benchmark Help) and the help output doesn't contains details about
    // custom defined flags like `--skip_expensive_benchmarks`, `--runtime_feature`, etc
    ::benchmark::PrintDefaultHelp();
  } else {
    // Passing the arguments of the program to Google Benchmark.
    // That way Google benchmark options would also be supported, along with the
    // custom defined custom flags
    ::benchmark::Initialize(&argc, argv);
  }

  TestEnvironment::initializeTestMain(argv[0]);

  // Suppressing non-error messages in benchmark tests. This hides warning
  // messages that appear when using a runtime feature when there isn't an initialized
  // runtime, and may have non-negligible impact on performance.
  // TODO(adisuissa): This should be configurable, similarly to unit tests.
  const spdlog::level::level_enum default_log_level = spdlog::level::err;
  Envoy::Logger::Registry::setLogLevel(default_log_level);

  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  TCLAP::CmdLine cmd("envoy-benchmark-test", ' ', "0.1");
  TCLAP::SwitchArg skip_switch("s", "skip_expensive_benchmarks",
                               "skip or minimize expensive benchmarks", cmd, false);
  TCLAP::MultiArg<std::string> runtime_features(
      "r", "runtime_feature", "runtime feature settings each of the form: <flag_name>:<flag_value>",
      false, "string", cmd);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
  } catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output
    // for --help and --version.
    return 0;
  }

  // Reduce logs so benchmark output is readable.
  Envoy::Thread::MutexBasicLockable lock;
  Logger::Context logging_context{spdlog::level::warn, Logger::Context::getFineGrainLogFormat(),
                                  lock, false};

  skip_expensive_benchmarks = skip_switch.getValue();

  TestScopedRuntime runtime;
  // Initialize scoped_runtime if a runtime_feature argument is present. This
  // allows benchmarks to use their own scoped_runtime in case no runtime flag is
  // passed as an argument.
  const auto& runtime_features_args = runtime_features.getValue();
  for (const absl::string_view runtime_feature_arg : runtime_features_args) {
    // Make sure the argument contains a single ":" character.
    const std::vector<std::string> runtime_feature_split = absl::StrSplit(runtime_feature_arg, ':');
    if (runtime_feature_split.size() != 2) {
      ENVOY_LOG_MISC(critical,
                     "Given runtime flag \"{}\" should have a single ':' separating the flag name "
                     "and its value.",
                     runtime_feature_arg);
      return 1;
    }
    const auto& feature_name = runtime_feature_split[0];
    const auto& feature_val = runtime_feature_split[1];
    runtime.mergeValues({{feature_name, feature_val}});
  }

  if (skip_expensive_benchmarks) {
    ENVOY_LOG_MISC(
        critical,
        "Expensive benchmarks are being skipped; see test/README.md for more information");
  }
  ::benchmark::RunSpecifiedBenchmarks();
  cleanup_hook();
}

void Envoy::benchmark::setCleanupHook(std::function<void()> hook) { cleanup_hook = hook; }
bool Envoy::benchmark::skipExpensiveBenchmarks() { return skip_expensive_benchmarks; }
