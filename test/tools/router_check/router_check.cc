// NOLINT(namespace-envoy)
#include <iostream>
#include <string>
#include <vector>

#include "source/common/common/thread.h"
#include "source/exe/platform_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/tools/router_check/router.h"
#include "test/tools/router_check/validation.pb.h"

bool hasFailures(
    const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult>& test_results) {
  for (const auto& test_result : test_results) {
    if (!test_result.test_passed()) {
      return true;
    }
  }
  return false;
}

int main(int argc, char* argv[]) {
  Envoy::Options options(argc, argv);
  Envoy::ScopedInjectableLoader<Envoy::Regex::Engine> engine(
      std::make_unique<Envoy::Regex::GoogleReEngine>());

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

    if (options.detailedCoverageReport()) {
      checktool.setDetailedCoverageReport();
    }

    const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
        checktool.compareEntries(options.testPath());
    if (!options.outputPath().empty()) {
      envoy::RouterCheckToolSchema::ValidationResult result;
      *result.mutable_test_results() = {test_results.begin(), test_results.end()};
      Envoy::TestEnvironment::writeStringToFileForTest(
          options.outputPath(), result.SerializeAsString(), /*fully_qualified_path=*/true);
    }
    // Test fails if routes do not match what is expected
    if (hasFailures(test_results)) {
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
