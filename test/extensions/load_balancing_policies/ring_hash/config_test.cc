#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.validate.h"

#include "source/extensions/load_balancing_policies/ring_hash/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RingHash {
namespace {

TEST(RingHashConfigTest, Validate) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Upstream::MockLoadBalancerFactoryContext> lb_factory_context;

  {
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("envoy.load_balancing_policies.ring_hash");
    envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash config_msg;
    config.mutable_typed_config()->PackFrom(config_msg);

    auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
    EXPECT_EQ("envoy.load_balancing_policies.ring_hash", factory.name());

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

  {
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("envoy.load_balancing_policies.ring_hash");
    envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash config_msg;
    config_msg.mutable_minimum_ring_size()->set_value(4);
    config_msg.mutable_maximum_ring_size()->set_value(2);

    config.mutable_typed_config()->PackFrom(config_msg);

    auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
    EXPECT_EQ("envoy.load_balancing_policies.ring_hash", factory.name());

    auto messsage_ptr = factory.createEmptyConfigProto();
    messsage_ptr->MergeFrom(config_msg);

    auto message_ptr = factory.createEmptyConfigProto();
    message_ptr->MergeFrom(config_msg);
    auto lb_config =
        factory.loadConfig(lb_factory_context, *message_ptr, context.messageValidationVisitor());

    EXPECT_THROW_WITH_MESSAGE(
        factory.create(*lb_config, cluster_info, main_thread_priority_set, context.runtime_loader_,
                       context.api_.random_, context.time_system_),
        EnvoyException, "ring hash: minimum_ring_size (4) > maximum_ring_size (2)");
  }

  {
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("envoy.load_balancing_policies.ring_hash");
    envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash config_msg;
    config_msg.mutable_minimum_ring_size()->set_value(0);

    config.mutable_typed_config()->PackFrom(config_msg);

    std::string err;
    EXPECT_EQ(Validate(config_msg, &err), false);
    EXPECT_EQ(err, "RingHashValidationError.MinimumRingSize: value must be "
                   "inside range [1, 8388608]");
  }
}

} // namespace
} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
