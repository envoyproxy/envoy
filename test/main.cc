// NOLINT(namespace-envoy)
#include "test/test_common/environment.h"
#include "test/test_runner.h"

#include "absl/debugging/symbolize.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

const char* __asan_default_options() {
  static char result[] = {"check_initialization_order=true strict_init_order=true"};
  return result;
}

// The main entry point (and the rest of this file) should have no logic in it,
// this allows overriding by site specific versions of main.cc.
int main(int argc, char** argv) {
#ifndef __APPLE__
  absl::InitializeSymbolizer(argv[0]);
#endif
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

  Envoy::TestEnvironment::setEnvVar("TEST_RUNDIR",
                                    (Envoy::TestEnvironment::getCheckedEnvVar("TEST_SRCDIR") + "/" +
                                     Envoy::TestEnvironment::getCheckedEnvVar("TEST_WORKSPACE")),
                                    1);

  // Select whether to test only for IPv4, IPv6, or both. The default is to
  // test for both. Options are {"v4only", "v6only", "all"}. Set
  // ENVOY_IP_TEST_VERSIONS to "v4only" if the system currently does not support IPv6 network
  // operations. Similarly set ENVOY_IP_TEST_VERSIONS to "v6only" if IPv4 has already been
  // phased out of network operations. Set to "all" (or don't set) if testing both
  // v4 and v6 addresses is desired. This feature is in progress and will be rolled out to all tests
  // in upcoming PRs.
  Envoy::TestEnvironment::setEnvVar("ENVOY_IP_TEST_VERSIONS", "all", 0);
  return Envoy::TestRunner::RunTests(argc, argv);
}
