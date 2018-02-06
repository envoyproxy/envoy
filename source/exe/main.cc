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
  try {
#ifdef ENVOY_HOT_RESTART
    Envoy::MainCommon main_common(argc, argv, true);
#else
    Envoy::MainCommon main_common(argc, argv, false);
#endif
    main_common.run();
  } catch (const Envoy::NoServingException& e) {
    return 0;
  } catch (const Envoy::MalformedArgvException& e) {
    return 1;
  } catch (const Envoy::EnvoyException& e) {
    return 1;
  }
  return 0;
}
