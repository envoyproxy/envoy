#include "source/server/admin/utils.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Server {

class UtilsTest : public testing::Test {
public:
  UtilsTest() {}
};

// Most utils paths are covered through other tests, these tests take of
// of special cases to get remaining coverage.
TEST(UtilsTest, BadServerState) {
  Utility::serverState(Init::Manager::State::Uninitialized, true);
  EXPECT_THROW_WITH_REGEX(Utility::serverState(static_cast<Init::Manager::State>(123), true),
                          EnvoyException, "unexpected server state");
}

} // namespace Server
} // namespace Envoy
