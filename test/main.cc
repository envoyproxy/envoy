#include "test/test_common/environment.h"
#include "test/test_runner.h"

// The main entry point (and the rest of this file) should have no logic in it,
// this allows overriding by site specific versions of main.cc.
int main(int argc, char** argv) {
  ::setenv("TEST_RUNDIR", TestEnvironment::runfilesDirectory().c_str(), 1);
  return TestRunner::RunTests(argc, argv);
}
