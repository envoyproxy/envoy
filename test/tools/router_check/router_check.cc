// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "source/common/common/thread.h"
#include "source/exe/platform_impl.h"

#include "test/test_common/test_runtime.h"
#include "test/tools/router_check/router.h"

int main(int argc, char* argv[]) {
  Envoy::Options options(argc, argv);

  const bool enforce_coverage = options.failUnder() != 0.0;
  // We need this to ensure WSAStartup is called on Windows
  Envoy::PlatformImpl platform_impl_;
  // Until we remove v2 API, the tool will warn but not fail.
  Envoy::TestDeprecatedV2Api _deprecated_v2_api;

  try {
    Envoy::RouterCheckTool checktool =
        Envoy::RouterCheckTool::create(options.configPath(), options.disableDeprecationCheck());

    if (options.isDetailed()) {
      checktool.setShowDetails();
    }

    if (options.onlyShowFailures()) {
      checktool.setOnlyShowFailures();
    }

    bool is_equal = checktool.compareEntries(options.testPath());
    // Test fails if routes do not match what is expected
    if (!is_equal) {
      return EXIT_FAILURE;
    }

    const double current_coverage = checktool.coverage(options.comprehensiveCoverage());
    std::cout << "Current route coverage: " << current_coverage << "%" << std::endl;
    if (enforce_coverage) {
      if (current_coverage < options.failUnder()) {
        std::cerr << "Failed to meet coverage requirement: " << options.failUnder() << "%"
                  << std::endl;
        return EXIT_FAILURE;
      }
    }
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
