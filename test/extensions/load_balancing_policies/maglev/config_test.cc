#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/maglev/v3/maglev.pb.h"

#include "source/extensions/load_balancing_policies/maglev/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Maglev {
namespace {

TEST(MaglevConfigTest, Validate) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;

  {
    envoy::config::core::v3::TypedExtensionConfig config;
    config.set_name("envoy.load_balancing_policies.maglev");
    envoy::extensions::load_balancing_policies::maglev::v3::Maglev config_msg;
    config.mutable_typed_config()->PackFrom(config_msg);

    auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
    EXPECT_EQ("envoy.load_balancing_policies.maglev", factory.name());

    auto lb_config = factory.loadConfig(context, *factory.createEmptyConfigProto()).value();
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
    config.set_name("envoy.load_balancing_policies.maglev");
    envoy::extensions::load_balancing_policies::maglev::v3::Maglev config_msg;
    config_msg.mutable_table_size()->set_value(4);
    auto* hash_policy = config_msg.mutable_consistent_hashing_lb_config()->add_hash_policy();
    *hash_policy->mutable_cookie()->mutable_name() = "test-cookie-name";
    *hash_policy->mutable_cookie()->mutable_path() = "/test/path";
    hash_policy->mutable_cookie()->mutable_ttl()->set_seconds(1000);

    config.mutable_typed_config()->PackFrom(config_msg);

    auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
    EXPECT_EQ("envoy.load_balancing_policies.maglev", factory.name());

    auto message_ptr = factory.createEmptyConfigProto();
    message_ptr->MergeFrom(config_msg);
    auto lb_config = factory.loadConfig(context, *message_ptr).value();

    EXPECT_THROW_WITH_MESSAGE(factory.create(*lb_config, cluster_info, main_thread_priority_set,
                                             context.runtime_loader_, context.api_.random_,
                                             context.time_system_),
                              EnvoyException, "The table size of maglev must be prime number");
  }
}

} // namespace
} // namespace Maglev
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
