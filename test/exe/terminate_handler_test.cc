#include "exe/terminate_handler.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(TerminateHandler, HandlerInstalledTest) {
  TerminateHandler handler;
  EXPECT_DEATH_LOG_TO_STDERR([]() -> void { std::terminate(); }(), ".*std::terminate called!.*");
}

} // namespace Envoy
