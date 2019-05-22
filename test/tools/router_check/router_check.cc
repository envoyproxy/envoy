// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "test/tools/router_check/router.h"

// TODO(jyotima): In the future, change this to use TCLAP
bool isArgument(int argc, char* argv[], const std::string& argument) {
  if (argc == 5 && (std::string(argv[3]) == argument || std::string(argv[4]) == argument)) {
    return true;
  }

  if (argc == 4 && std::string(argv[3]) == argument) {
    return true;
  }

  return false;
}

int main(int argc, char* argv[]) {
  if (argc < 3 || argc > 5) {
    return EXIT_FAILURE;
  }

  try {
    Envoy::RouterCheckTool checktool = Envoy::RouterCheckTool::create(argv[1]);

    if (isArgument(argc, argv, "--details")) {
      checktool.setShowDetails();
    }

    bool is_equal = true;
    if (isArgument(argc, argv, "--useproto")) {
      is_equal = checktool.compareEntries(argv[2]);
    } else {
      // TODO(jyotima): Remove this code path once the json schema code path is deprecated.
      is_equal = checktool.compareEntriesInJson(argv[2]);
    }
    // Test fails if routes do not match what is expected
    if (!is_equal) {
      return EXIT_FAILURE;
    }
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
