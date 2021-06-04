#include "source/exe/terminate_handler.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(TerminateHandlerDeathTest, HandlerInstalledTest) {
  TerminateHandler handler;
  EXPECT_DEATH([]() -> void { std::terminate(); }(), ".*std::terminate called!.*");
}

} // namespace Envoy
