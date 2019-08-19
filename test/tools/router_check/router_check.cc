// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "test/tools/router_check/router.h"

int main(int argc, char* argv[]) {
  Envoy::Options options(argc, argv);

  const bool enforce_coverage = options.failUnder() != 0.0;
  try {
    Envoy::RouterCheckTool checktool =
        options.isProto() ? Envoy::RouterCheckTool::create(options.configPath())
                          : Envoy::RouterCheckTool::create(options.unlabelledConfigPath());

    if (options.isDetailed()) {
      checktool.setShowDetails();
    }

    bool is_equal = options.isProto()
                        ? checktool.compareEntries(options.testPath())
                        : checktool.compareEntriesInJson(options.unlabelledTestPath());
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
