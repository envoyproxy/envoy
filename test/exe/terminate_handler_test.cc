#include "exe/terminate_handler.h"

#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

namespace Envoy {

TEST_F(TestBase, TerminateHandlerDeathTest_HandlerInstalledTest) {
  TerminateHandler handler;
  EXPECT_DEATH_LOG_TO_STDERR([]() -> void { std::terminate(); }(), ".*std::terminate called!.*");
}

} // namespace Envoy
