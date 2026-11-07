#include "test/mocks/event/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

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
  NiceMock<Event::MockDispatcher> worker_dispatcher("worker_0");
  context.thread_local_.setDispatcher(&worker_dispatcher);
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

TEST(PerWorkerSubsetFactoryTest, WorkerIdentityIsStableAcrossLbRecreation) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Event::MockDispatcher> worker_dispatcher("worker_0");
  context.thread_local_.setDispatcher(&worker_dispatcher);
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> priority_set;

  PerWorkerSubsetLbProto proto;
  proto.set_host_selection_strategy(PerWorkerSubsetLbProto::SIMPLE_ROUND_ROBIN);

  Factory factory;
  auto config = factory.loadConfig(context, proto);
  ASSERT_TRUE(config.ok());

  const auto* typed = dynamic_cast<const TypedPerWorkerSubsetLbConfig*>(config.value().get());
  ASSERT_NE(typed, nullptr);

  PerWorkerSubsetCreator creator;
  const auto create_lb = [&]() {
    return creator(Upstream::LoadBalancerParams{priority_set, nullptr},
                   OptRef<const Upstream::LoadBalancerConfig>(*config.value()), cluster_info,
                   priority_set, context.runtime_loader_, context.api_.random_,
                   context.time_system_);
  };

  auto first = create_lb();
  const auto* first_typed = dynamic_cast<const PerWorkerSubsetLoadBalancer*>(first.get());
  ASSERT_NE(first_typed, nullptr);
  EXPECT_EQ(first_typed->workerIdForTest(), 0);

  first.reset();
  auto recreated = create_lb();
  const auto* recreated_typed = dynamic_cast<const PerWorkerSubsetLoadBalancer*>(recreated.get());
  ASSERT_NE(recreated_typed, nullptr);
  EXPECT_EQ(recreated_typed->workerIdForTest(), 0);
}

} // namespace
} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
