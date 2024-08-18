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

  const bool enforce_coverage = options.fail_under != 0.0;
  // We need this to ensure WSAStartup is called on Windows
  Envoy::PlatformImpl platform_impl_;
  // Until we remove v2 API, the tool will warn but not fail.
  Envoy::TestDeprecatedV2Api _deprecated_v2_api;

  try {
    Envoy::RouterCheckTool checktool = Envoy::RouterCheckTool::create(options);
    const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
        checktool.compareEntries();
    if (!options.output_path.empty()) {
      envoy::RouterCheckToolSchema::ValidationResult result;
      *result.mutable_test_results() = {test_results.begin(), test_results.end()};
      Envoy::TestEnvironment::writeStringToFileForTest(
          options.output_path, result.SerializeAsString(), /*fully_qualified_path=*/true);
    }
    // Test fails if routes do not match what is expected
    if (hasFailures(test_results)) {
      return EXIT_FAILURE;
    }

    const double current_coverage = checktool.coverage();
    std::cout << "Current route coverage: " << current_coverage << "%" << std::endl;
    if (enforce_coverage) {
      if (current_coverage < options.fail_under) {
        std::cerr << "Failed to meet coverage requirement: " << options.fail_under << "%"
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
