#include "examples/cc/fetch/fetch.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];

const char build_scm_revision[] = "0";
const char build_scm_status[] = "test";

int main(int argc, char** argv) {
  Envoy::Platform::Fetch fetch(argc, argv);
  fetch.Run();
  exit(0);
}
