// NOLINT(namespace-envoy)
// This is an Envoy driver for benchmarks.
#include "envoy/common/platform.h"

#include "benchmark/benchmark.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif
#include "common/common/assert.h"

#include "absl/debugging/symbolize.h"

#if defined(WIN32)
static void NoopInvalidParameterHandler(const wchar_t* expression, const wchar_t* function,
                                        const wchar_t* file, unsigned int line,
                                        uintptr_t pReserved) {
  return;
}
#endif

// Boilerplate main(), which discovers benchmarks and runs them.
int main(int argc, char** argv) {
#if defined(WIN32)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  _set_invalid_parameter_handler(NoopInvalidParameterHandler);

  WSADATA wsa_data;
  const WORD version_requested = MAKEWORD(2, 2);
  RELEASE_ASSERT(WSAStartup(version_requested, &wsa_data) == 0, "");
#endif

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
