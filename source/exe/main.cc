#include "exe/main_common.h"

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */
int main(int argc, const char** argv) {
#ifdef ENVOY_HOT_RESTART
  bool enable_hot_restart = true;
#else
  bool enable_hot_restart = false;
#endif
  std::unique_ptr<Envoy::MainCommon> main_common;
  try {
    main_common = std::make_unique<Envoy::MainCommon>(argc, argv, enable_hot_restart);
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    return EXIT_FAILURE;
  }
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
