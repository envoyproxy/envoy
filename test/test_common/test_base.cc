#include "test/test_common/test_base.h"

#include "common/common/assert.h"

#include "test/test_common/global.h"

namespace Envoy {

void TestBase::checkSingletonQuiescensce() {
  // Check that all singletons have been destroyed.
  std::string active_singletons = Envoy::Test::Globals::describeActiveSingletons();
  RELEASE_ASSERT(active_singletons.empty(),
                 absl::StrCat("FAIL: Active singletons exist:\n", active_singletons));
}

TestBase::~TestBase() { checkSingletonQuiescensce(); }

} // namespace Envoy
