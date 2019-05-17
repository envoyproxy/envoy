// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "test/tools/router_check/router.h"

int main(int argc, char* argv[]) {
  if (argc < 3 || argc > 5) {
    return EXIT_FAILURE;
  }

  try {
    Envoy::RouterCheckTool checktool = Envoy::RouterCheckTool::create(argv[1]);

    if (argc >= 4 && std::string(argv[3]) == "--details") {
      checktool.setShowDetails();
    }

    bool assert = true;
    if (argc == 5 && std::string(argv[4]) == "--useproto") {
      assert = checktool.compareEntries(argv[2]);
    } else {
      assert = checktool.compareEntriesInJson(argv[2]);
    }
    // Test fails if routes do not match what is expected
    if (!assert) {
      return EXIT_FAILURE;
    }
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
