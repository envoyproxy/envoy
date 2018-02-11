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
  try {
#ifdef ENVOY_HOT_RESTART
    Envoy::MainCommon main_common(argc, argv, true);
#else
    Envoy::MainCommon main_common(argc, argv, false);
#endif
    return main_common.run() ? 0 : 1;
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    std::cerr << "MalformedArgvException: " << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    std::cerr << "EnvoyException: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
