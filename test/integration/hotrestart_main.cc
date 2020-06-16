#include "common/stats/utility.h"

#include "exe/main_common.h"

// NOLINT(namespace-envoy)

/**
 * Custom main() for hotrestart_test. This should be identical to
 * source/exe/main.cc, except for the registration and increment of a new gauge
 * specifically for hot_restart.test.sh.
 */
int main(int argc, char** argv) {
  return Envoy::MainCommon::main(argc, argv, [](Envoy::Server::Instance& server) {
    // Creates a gauge that will be incremented once and then never touched. This is
    // for testing parent-gauge accumulation in hot_restart_test.sh.
    Envoy::Stats::Utility::gaugeFromElements(server.stats(),
                                             {Envoy::Stats::DynamicName("hotrestart_test_gauge")},
                                             Envoy::Stats::Gauge::ImportMode::Accumulate)
        .inc();
  });
}
