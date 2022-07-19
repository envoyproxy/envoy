#include <utility>
#include <vector>

#include "test/mocks/server/factory_context.h"

#include "contrib/network/connection_balance/random/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Random {

class ConfigTest : public testing::Test {
  protected:
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(ConfigTest, CreateEmptyConfigProto) {
 auto config =  factory_.createEmptyConfigProto();
 EXPECT_NE(nil, config);
}

TEST_F(ConfigTest, CreateEmptyConfigProto) {
 auto balancer =  factory_.createConnectionBalancerFromProto(context_);
}

} // namespace Random
} // namespace Extensions
} // namespace Envoy
