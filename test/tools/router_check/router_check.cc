#include "test/precompiled/precompiled_test.h"
#include "test/tools/router_check/router.h"

int main(int argc, char* argv[]) {

  if (argc < 3 || argc > 4) {
    return EXIT_FAILURE;
  }

  RouterCheckTool checktool;

  // Load router config json
  if (!checktool.initializeFromConfig(argv[1])) {
    return EXIT_FAILURE;
  }

  // TODO(hennna): Switch to gflags
  if (argc == 4 && std::string(argv[3]) == "--details") {
    checktool.setShowDetails();
  }

  // Load tool config json
  // Test fails if routes do not match what is expected
  if (!checktool.compareEntriesInJson(argv[2])) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
