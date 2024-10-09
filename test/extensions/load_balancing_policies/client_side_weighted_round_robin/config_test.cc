#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/config.h"

//#include "testing/base/public/gunit.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClientSideWeightedRoundRobin {
namespace {

TEST(ClientSideWeightedRoundRobinConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Event::MockDispatcher> mock_thread_dispatcher;
  NiceMock<Upstream::MockLoadBalancerFactoryContext> lb_factory_context;
  ON_CALL(lb_factory_context, mainThreadDispatcher())
      .WillByDefault(ReturnRef(mock_thread_dispatcher));

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.client_side_weighted_round_robin");
  envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
      ClientSideWeightedRoundRobin config_msg;
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.client_side_weighted_round_robin", factory.name());

  auto lb_config = factory.loadConfig(lb_factory_context, *factory.createEmptyConfigProto(),
                                      context.messageValidationVisitor());

  auto thread_aware_lb =
      factory.create(*lb_config, cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_NE(nullptr, thread_aware_lb);

  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  auto thread_local_lb_factory = thread_aware_lb->factory();
  EXPECT_NE(nullptr, thread_local_lb_factory);

  auto thread_local_lb = thread_local_lb_factory->create({thread_local_priority_set, nullptr});
  EXPECT_NE(nullptr, thread_local_lb);
}

} // namespace
} // namespace ClientSideWeightedRoundRobin
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
