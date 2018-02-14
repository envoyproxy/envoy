#include "exe/main_common.h"

#include "common/common/perf_annotation.h"

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */
int main(int argc, char** argv) {
  // TODO(jmarantz,): remove this hack when #2576 lands.
#ifdef ENVOY_HOT_RESTART
  bool enable_hot_restart = true;
  if ((argc > 1) && (strcmp(argv[1], "--disable_hot_restart") == 0)) {
    enable_hot_restart = false;
    memmove(&argv[1], &argv[2], (argc - 2) * sizeof(char*));
    --argc;
    argv[argc] = nullptr;
  }
#else
  constexpr bool enable_hot_restart = false;
#endif

  std::unique_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    main_common = std::make_unique<Envoy::MainCommon>(argc, argv, enable_hot_restart);
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    return EXIT_FAILURE;
  }

  PERF_DUMP();
}
