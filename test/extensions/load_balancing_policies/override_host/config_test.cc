#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/override_host/v3/override_host.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/utility.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/load_balancing_policies/override_host/test_lb.pb.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {
namespace {

using ::Envoy::EnvoyException;
using ::Envoy::Config::Utility;
using ::envoy::extensions::load_balancing_policies::override_host::v3::OverrideHost;
using ::Envoy::Upstream::MockHostSet;
using ::test::load_balancing_policies::override_host::Config;
using ::testing::HasSubstr;

TEST(OverrideHostLbonfigTest, NoFallbackLb) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  config_msg.add_override_host_sources()->set_header("x-foo");
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.override_host", factory.name());

  EXPECT_THROW_WITH_REGEX(factory.loadConfig(context, config_msg).value(), EnvoyException,
                          "value is required");
}

TEST(OverrideHostLbonfigTest, NoFallbackPolicies) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  config_msg.add_override_host_sources()->set_header("x-foo");
  config_msg.mutable_fallback_policy();
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                               "dynamic forwarding LB: didn't find a registered "
                                               "fallback load balancer factory with names from "));
}

TEST(OverrideHostLbonfigTest, NoPrimaryOverideSources) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;

  Protobuf::Struct invalid_policy;
  auto* typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(invalid_policy);
  typed_extension_config->set_name("existent_policy");
  Config fallback_picker_config;
  typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.override_host.test");

  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  EXPECT_THROW_WITH_REGEX(factory.loadConfig(context, config_msg).value(), EnvoyException,
                          "value must contain at least 1 item");
}

TEST(OverrideHostLbonfigTest, FirstValidFallbackPolicyIsUsed) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  config_msg.add_override_host_sources()->set_header("x-foo");

  Protobuf::Struct invalid_policy;
  auto* typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(invalid_policy);
  typed_extension_config->set_name("existent_policy");
  Config fallback_picker_config;
  typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.override_host.test");

  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_TRUE(result.ok());
}

TEST(OverrideHostLbonfigTest, EmptyPrimaryOverrideSource) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  // Do not set either host or metadata keys
  config_msg.add_override_host_sources();

  Protobuf::Struct invalid_policy;
  auto* typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(invalid_policy);
  typed_extension_config->set_name("existent_policy");
  Config fallback_picker_config;
  typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.override_host.test");

  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                               "Empty override source"));
}

TEST(OverrideHostLbonfigTest, HeaderAndMetadataInTheSameOverrideSource) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  // Do not set either host or metadata keys
  auto* primary_host_source = config_msg.add_override_host_sources();
  primary_host_source->set_header("x-foo");
  auto* metadata_key = primary_host_source->mutable_metadata();
  metadata_key->set_key("x-bar");
  metadata_key->add_path()->set_key("a/b/c");

  Protobuf::Struct invalid_policy;
  auto* typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(invalid_policy);
  typed_extension_config->set_name("existent_policy");
  Config fallback_picker_config;
  typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.override_host.test");

  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto result = factory.loadConfig(context, config_msg);
  EXPECT_THAT(result, StatusHelpers::HasStatus(absl::StatusCode::kInvalidArgument,
                                               "Only one override source must be set"));
}

TEST(OverrideHostLbonfigTest, FallbackLbCalledToChooseHost) {
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> context;
  auto cluster_info = std::make_shared<NiceMock<Envoy::Upstream::MockClusterInfo>>();
  NiceMock<Envoy::Upstream::MockPrioritySet> main_thread_priority_set;
  NiceMock<Envoy::Upstream::MockPrioritySet> thread_local_priority_set;
  NiceMock<Envoy::Upstream::MockLoadBalancerContext> load_balancer_context;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancers.override_host");
  OverrideHost config_msg;
  config_msg.add_override_host_sources()->set_header("x-foo");
  Config fallback_picker_config;
  auto* typed_extension_config =
      config_msg.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
  typed_extension_config->mutable_typed_config()->PackFrom(fallback_picker_config);
  typed_extension_config->set_name("envoy.load_balancers.override_host.test");
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Utility::getAndCheckFactory<::Envoy::Upstream::TypedLoadBalancerFactory>(config);

  auto lb_config = factory.loadConfig(context, config_msg).value();

  auto thread_aware_lb =
      factory.create(*lb_config, *cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_NE(thread_aware_lb, nullptr);

  ASSERT_TRUE(thread_aware_lb->initialize().ok());

  auto thread_local_lb_factory = thread_aware_lb->factory();
  EXPECT_NE(thread_local_lb_factory, nullptr);

  MockHostSet* host_set = thread_local_priority_set.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(cluster_info, "tcp://127.0.0.1:80")};
  host_set->runCallbacks(host_set->hosts_, {});
  auto thread_local_lb = thread_local_lb_factory->create({thread_local_priority_set, nullptr});
  EXPECT_NE(thread_local_lb, nullptr);

  auto host = thread_local_lb->chooseHost(&load_balancer_context).host;
  EXPECT_NE(host, nullptr);
  EXPECT_EQ(host->address()->asString(), "127.0.0.1:80");
}

} // namespace
} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
