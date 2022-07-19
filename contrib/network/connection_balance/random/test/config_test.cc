#include "test/mocks/server/factory_context.h"
#include "envoy/registry/registry.h"
#include "test/test_common/registry.h"
#include "contrib/network/connection_balance/random/source/config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Random {

class ConfigTest : public testing::Test {
protected:
  RandomConnectionBalanceFactory factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(ConfigTest, CreateEmptyConfigProto) {
  auto config = factory_.createEmptyConfigProto();
  EXPECT_NE(config, nullptr);
}

TEST_F(ConfigTest, CreateConnectionBalancerFromProto) {
  envoy::config::core::v3::TypedExtensionConfig config;
  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory_);
  auto balancer = factory_.createConnectionBalancerFromProto(*message.get(), context_);
  EXPECT_NE(balancer, nullptr);
}

} // namespace Random
} // namespace Extensions
} // namespace Envoy
