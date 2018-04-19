#include "exe/terminate_handler.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(TerminateHandler, HandlerInstalledTest) {
  TerminateHandler handler;
  EXPECT_DEATH([]() -> void { std::terminate(); }(),
               ".*Envoy::TerminateHandler::logOnTerminate().*");
}

} // namespace Envoy
