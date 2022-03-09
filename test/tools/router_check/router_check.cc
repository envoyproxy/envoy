// NOLINT(namespace-envoy)
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"
#include "source/exe/platform_impl.h"

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

void writeOutput(const envoy::RouterCheckToolSchema::ValidationResult& result,
                 const std::string& filepath) {
  auto stats = std::make_unique<Envoy::Stats::IsolatedStoreImpl>();
  auto api = Envoy::Api::createApiForTest(*stats);
  static constexpr Envoy::Filesystem::FlagSet DefaultFlags{
      1 << Envoy::Filesystem::File::Operation::Write |
      1 << Envoy::Filesystem::File::Operation::Create};
  Envoy::Filesystem::FilePathAndType file_info{Envoy::Filesystem::DestinationType::File, filepath};
  auto file = api->fileSystem().createFile(file_info);
  if (!file || !file->open(DefaultFlags).return_value_) {
    throw Envoy::EnvoyException(fmt::format("Failed to open file for write {}", filepath));
  }
  const auto& write_result = file->write(result.SerializeAsString());
  if (!write_result.ok()) {
    throw Envoy::EnvoyException(fmt::format("Failed to write output to file {}, error: {}",
                                            filepath, write_result.err_->getErrorDetails()));
  }
}

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

    const std::vector<envoy::RouterCheckToolSchema::ValidationItemResult> test_results =
        checktool.compareEntries(options.testPath());
    if (!options.outputPath().empty()) {
      envoy::RouterCheckToolSchema::ValidationResult result;
      *result.mutable_test_results() = {test_results.begin(), test_results.end()};
      writeOutput(result, options.outputPath());
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
