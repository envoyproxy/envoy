#include "exe/main_common.h"

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */
int main(int argc, char** argv) {
  int exit_status = EXIT_SUCCESS;
  std::unique_ptr<Envoy::MainCommon> main_common(
      Envoy::MainCommon::create(argc, argv, exit_status));
  if (main_common.get() == nullptr) {
    return exit_status;
  }
  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostis.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
