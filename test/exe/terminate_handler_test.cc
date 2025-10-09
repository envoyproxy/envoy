#include "source/exe/terminate_handler.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class TerminateHandlerTest : public testing::Test {
public:
  void logException(const std::exception_ptr e) { handler.logException(e); };

  TerminateHandler handler;
};

TEST(TerminateHandlerDeathTest, HandlerInstalledTest) {
  TerminateHandler handler;
  EXPECT_DEATH([]() -> void { std::terminate(); }(), ".*std::terminate called!.*");
}

TEST_F(TerminateHandlerTest, LogsEnvoyException) {
  EXPECT_LOG_CONTAINS("critical",
                      "std::terminate called! Uncaught EnvoyException 'boom', see trace.",
                      logException(std::make_exception_ptr(EnvoyException("boom"))));
}

TEST_F(TerminateHandlerTest, LogsUnknownException) {
  EXPECT_LOG_CONTAINS("critical", "std::terminate called! Uncaught unknown exception, see trace.",
                      logException(std::make_exception_ptr("Boom")));
}

TEST_F(TerminateHandlerTest, HandlesNullptrCurrentException) {
  EXPECT_LOG_CONTAINS("critical", "std::terminate called! See trace.", logException(nullptr));
}

} // namespace Envoy
