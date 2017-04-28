#include "test/test_common/environment.h"
#include "test/test_runner.h"

// The main entry point (and the rest of this file) should have no logic in it,
// this allows overriding by site specific versions of main.cc.
int main(int argc, char** argv) {
  ::setenv("TEST_RUNDIR", TestEnvironment::runfilesDirectory().c_str(), 1);
  // Select whether to test only for IPv4, IPv6, or both. The default is to
  // test for both. Options are {"v4only", "v6only", "all"}. Set IP_TEST_TYPE
  // to "v4only" if the system currently does not support IPv6 network
  // operations. Similary set IP_TEST_TYPE to "v6only" if IPv4 has already been
  // phased out of network operations. Set to "all" (or don't set) if testing both
  // v4 and v6 addresses is desired. This feature will be rolled out to all tests
  // in upcoming PRs but currently only applies to listen_socket_impl_test
  // and listener_impl_test tests.
  ::setenv("IP_TEST_TYPE", "all", 1);
  return TestRunner::RunTests(argc, argv);
}
