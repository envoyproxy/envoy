#include "test/test_common/test_base.h"

#include "gtest/gtest.h"

namespace Envoy {

class TestTimeSystemTest : public TestBase {
protected:
};

// TODO(jmarantz): This just validates we can still build tests that
// depend on TestBase for an interim time period. This test, as well
// as test_base.h, will be removed in a future PR.
TEST_F(TestBase, GracePeriod) {}

} // namespace Envoy
