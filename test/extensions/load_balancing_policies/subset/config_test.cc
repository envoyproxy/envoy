#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/subset/v3/subset.pb.h"

#include "source/extensions/load_balancing_policies/subset/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Subset {
namespace {

TEST(SubsetConfigTest, SubsetConfigTest) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Upstream::MockLoadBalancerFactoryContext> lb_factory_context;

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.subset");
  auto config_msg =
      std::make_unique<envoy::extensions::load_balancing_policies::subset::v3::Subset>();

  const std::string config_yaml = R"EOF(
    fallback_policy: ANY_ENDPOINT
    subset_selectors:
      - keys:
          - "version"
          - "stage"
        fallback_policy: NO_FALLBACK
      - keys:
          - "version"
        fallback_policy: ANY_ENDPOINT
    list_as_any: true
    subset_lb_policy:
      policies:
        - typed_extension_config:
            name: envoy.load_balancing_policies.random
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.load_balancing_policies.random.v3.Random
    )EOF";
  TestUtility::loadFromYaml(config_yaml, *config_msg);
  config.mutable_typed_config()->PackFrom(*config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.subset", factory.name());

  auto lb_config =
      factory.loadConfig(lb_factory_context, *config_msg, context.messageValidationVisitor());

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

TEST(SubsetConfigTest, SubsetConfigTestWithUnknownSubsetLoadBalancingPolicy) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Upstream::MockLoadBalancerFactoryContext> lb_factory_context;

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.subset");
  auto config_msg =
      std::make_unique<envoy::extensions::load_balancing_policies::subset::v3::Subset>();

  const std::string config_yaml = R"EOF(
    fallback_policy: ANY_ENDPOINT
    subset_selectors:
      - keys:
          - "version"
          - "stage"
        fallback_policy: NO_FALLBACK
      - keys:
          - "version"
        fallback_policy: ANY_ENDPOINT
    list_as_any: true
    subset_lb_policy:
      policies:
        - typed_extension_config:
            name: envoy.load_balancing_policies.unknown
    )EOF";
  TestUtility::loadFromYaml(config_yaml, *config_msg);
  config.mutable_typed_config()->PackFrom(*config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.subset", factory.name());

  EXPECT_THROW_WITH_MESSAGE(
      factory.loadConfig(lb_factory_context, *config_msg, context.messageValidationVisitor()),
      EnvoyException,
      "cluster: didn't find a registered load balancer factory implementation for subset lb with "
      "names from [envoy.load_balancing_policies.unknown]");
}

} // namespace
} // namespace Subset
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
