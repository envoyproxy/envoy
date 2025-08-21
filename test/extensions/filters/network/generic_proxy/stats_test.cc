#include "source/extensions/filters/network/generic_proxy/stats.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

TEST(StatsTest, StatsTest) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  CodeOrFlags code_or_flags(factory_context);

  // Max code is 999 for now.
  {
    EXPECT_EQ("-",
              factory_context.store_.symbolTable().toString(code_or_flags.statNameFromCode(1000)));
  }
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
