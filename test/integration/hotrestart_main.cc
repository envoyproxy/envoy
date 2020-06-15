#include "common/stats/utility.h"

#include "exe/main_common.h"

#include "absl/debugging/symbolize.h"

// NOLINT(namespace-envoy)

/**
 * Custom main() for hotrestart_test. This should be identical to
 * source/exe/main.cc, except for the registration and increment of a new gauge
 * specifically for hot_restart.test.sh.
 *
 * If this test starts failing to run or build, please ensure that all changes
 * from source/exe/main.cc are incorporated in this file.
 */
int main(int argc, char** argv) {
#ifndef __APPLE__
  // absl::Symbolize mostly works without this, but this improves corner case
  // handling, such as running in a chroot jail.
  absl::InitializeSymbolizer(argv[0]);
#endif
  std::unique_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    main_common = std::make_unique<Envoy::MainCommon>(argc, argv);

    // This block is specific to hotrestart_main.cc, and should be the only
    // functional difference with source/exe/main.cc.
    Envoy::Server::Instance* server = main_common->server();
    if (server != nullptr) {
      // Creates a gauge that will be incremented once and then never touched. This is
      // for testing parent-gauge accumulation in hot_restart_test.sh.
      Envoy::Stats::Utility::gaugeFromElements(server->stats(),
                                               {Envoy::Stats::DynamicName("hotrestart_test_gauge")},
                                               Envoy::Stats::Gauge::ImportMode::Accumulate)
          .inc();
    }
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostics.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
