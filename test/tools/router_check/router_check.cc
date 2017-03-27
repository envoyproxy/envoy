#include "router.h"
#include "test/precompiled/precompiled_test.h"

int main(int argc, char* argv[]) {

  if (argc < 3 || argc > 4) {
    return -1;
  }

  RouterCheckTool checktool;

  if (!checktool.create(argv[1])) {
    return -1;
  }

  // TODO(hennna): Switch to gflags
  if (argc == 4 && std::string(argv[3]) == "--details") {
    checktool.showDetails();
  }

  checktool.compareEntriesInJson(argv[2]);

  // Print out total matches and conflicts
  std::cout << "Total Y:" << checktool.getYes() << " N:" << checktool.getNo() << std::endl;

  return 0;
}
