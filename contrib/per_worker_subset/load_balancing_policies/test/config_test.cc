#include "test/mocks/server/server_factory_context.h"

#include "contrib/per_worker_subset/load_balancing_policies/source/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {
namespace {

TEST(PerWorkerSubsetFactoryTest, UsesProcessLocalRandomSeed) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context.api_.random_, random()).WillOnce(Return(123456));

  PerWorkerSubsetLbProto proto;
  proto.set_host_selection_strategy(PerWorkerSubsetLbProto::SIMPLE_ROUND_ROBIN);

  Factory factory;
  auto config = factory.loadConfig(context, proto);
  ASSERT_TRUE(config.ok());

  const auto* typed = dynamic_cast<const TypedPerWorkerSubsetLbConfig*>(config.value().get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->envoy_seed_, 123456);
}

} // namespace
} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
