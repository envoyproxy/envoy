#include "test/test_common/test_base.h"

#include "test/test_common/global.h"

namespace Envoy {

bool TestBase::checkSingletonQuiescensce() {
  // Check that all singletons have been destroyed.
  std::string active_singletons = Envoy::Test::Globals::describeActiveSingletons();
  if (!active_singletons.empty()) {
    std::cerr << "\n\nFAIL: Active singletons exist:\n" << active_singletons << std::endl;
    return false;
  }
  return true;
}

TestBase::~TestBase() { checkSingletonQuiescensce(); }

} // namespace Envoy
