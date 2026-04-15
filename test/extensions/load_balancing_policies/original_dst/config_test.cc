#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/load_balancing_policies/original_dst/v3/original_dst.pb.h"

#include "source/extensions/load_balancing_policies/original_dst/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OriginalDst {
namespace {

TEST(OriginalDstConfigTest, FactoryRegistrationAndNullLb) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<Upstream::MockClusterInfo> cluster_info;
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set;

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.original_dst");
  OriginalDstLbProto config_msg;
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);
  EXPECT_EQ("envoy.load_balancing_policies.original_dst", factory.name());

  // The original_dst LB policy is cluster-provided, so create() returns nullptr.
  auto thread_aware_lb =
      factory.create({}, cluster_info, main_thread_priority_set, context.runtime_loader_,
                     context.api_.random_, context.time_system_);
  EXPECT_EQ(nullptr, thread_aware_lb);
}

TEST(OriginalDstConfigTest, LoadConfigFromProto) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.original_dst");
  OriginalDstLbProto config_msg;
  config_msg.set_use_http_header(true);
  config_msg.set_http_header_name("x-custom-dst");
  config_msg.mutable_upstream_port_override()->set_value(8080);
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);

  auto lb_config_or_error = factory.loadConfig(context, config_msg);
  ASSERT_TRUE(lb_config_or_error.ok());
  auto* lb_config = dynamic_cast<const OriginalDstLbConfig*>(lb_config_or_error.value().get());
  ASSERT_NE(nullptr, lb_config);

  EXPECT_TRUE(lb_config->useHttpHeader());
  EXPECT_EQ("x-custom-dst", lb_config->httpHeaderName());
  ASSERT_TRUE(lb_config->upstreamPortOverride().has_value());
  EXPECT_EQ(8080, lb_config->upstreamPortOverride().value());
  EXPECT_FALSE(lb_config->hasMetadataKey());
}

TEST(OriginalDstConfigTest, LoadConfigFromProtoWithMetadataKey) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  OriginalDstLbProto config_msg;
  config_msg.set_use_http_header(false);
  auto* metadata_key = config_msg.mutable_metadata_key();
  metadata_key->set_key("envoy.lb");
  metadata_key->add_path()->set_key("dst_address");

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.original_dst");
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);

  auto lb_config_or_error = factory.loadConfig(context, config_msg);
  ASSERT_TRUE(lb_config_or_error.ok());
  auto* lb_config = dynamic_cast<const OriginalDstLbConfig*>(lb_config_or_error.value().get());
  ASSERT_NE(nullptr, lb_config);

  EXPECT_FALSE(lb_config->useHttpHeader());
  EXPECT_TRUE(lb_config->hasMetadataKey());
  EXPECT_EQ("envoy.lb", lb_config->metadataKey().key());
  EXPECT_FALSE(lb_config->upstreamPortOverride().has_value());
}

TEST(OriginalDstConfigTest, LoadConfigFromProtoDefaults) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  OriginalDstLbProto config_msg; // All defaults.

  envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("envoy.load_balancing_policies.original_dst");
  config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory = Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(config);

  auto lb_config_or_error = factory.loadConfig(context, config_msg);
  ASSERT_TRUE(lb_config_or_error.ok());
  auto* lb_config = dynamic_cast<const OriginalDstLbConfig*>(lb_config_or_error.value().get());
  ASSERT_NE(nullptr, lb_config);

  EXPECT_FALSE(lb_config->useHttpHeader());
  EXPECT_EQ("", lb_config->httpHeaderName());
  EXPECT_FALSE(lb_config->upstreamPortOverride().has_value());
  EXPECT_FALSE(lb_config->hasMetadataKey());
}

TEST(OriginalDstConfigTest, LoadLegacyConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("test_cluster");
  cluster.set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
  auto* lb_config = cluster.mutable_original_dst_lb_config();
  lb_config->set_use_http_header(true);
  lb_config->set_http_header_name("x-original-dst");
  lb_config->mutable_upstream_port_override()->set_value(443);

  envoy::config::core::v3::TypedExtensionConfig ext_config;
  ext_config.set_name("envoy.load_balancing_policies.original_dst");
  OriginalDstLbProto config_msg;
  ext_config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(ext_config);

  auto result = factory.loadLegacy(context, cluster);
  ASSERT_TRUE(result.ok());
  auto* original_dst_config = dynamic_cast<const OriginalDstLbConfig*>(result.value().get());
  ASSERT_NE(nullptr, original_dst_config);

  EXPECT_TRUE(original_dst_config->useHttpHeader());
  EXPECT_EQ("x-original-dst", original_dst_config->httpHeaderName());
  ASSERT_TRUE(original_dst_config->upstreamPortOverride().has_value());
  EXPECT_EQ(443, original_dst_config->upstreamPortOverride().value());
  EXPECT_FALSE(original_dst_config->hasMetadataKey());
}

TEST(OriginalDstConfigTest, LoadLegacyConfigDefaults) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("test_cluster");
  cluster.set_type(envoy::config::cluster::v3::Cluster::ORIGINAL_DST);
  // No original_dst_lb_config set — all defaults.

  envoy::config::core::v3::TypedExtensionConfig ext_config;
  ext_config.set_name("envoy.load_balancing_policies.original_dst");
  OriginalDstLbProto config_msg;
  ext_config.mutable_typed_config()->PackFrom(config_msg);

  auto& factory =
      Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(ext_config);

  auto result = factory.loadLegacy(context, cluster);
  ASSERT_TRUE(result.ok());
  auto* original_dst_config = dynamic_cast<const OriginalDstLbConfig*>(result.value().get());
  ASSERT_NE(nullptr, original_dst_config);

  EXPECT_FALSE(original_dst_config->useHttpHeader());
  EXPECT_EQ("", original_dst_config->httpHeaderName());
  EXPECT_FALSE(original_dst_config->upstreamPortOverride().has_value());
  EXPECT_FALSE(original_dst_config->hasMetadataKey());
}

} // namespace
} // namespace OriginalDst
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
