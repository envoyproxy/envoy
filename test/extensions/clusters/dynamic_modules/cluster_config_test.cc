#include "source/extensions/clusters/dynamic_modules/cluster_config.h"
#include "source/extensions/clusters/dynamic_modules/factory.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;

class DynamicModuleClusterConfigTest : public testing::Test {
protected:
  void SetUp() override {
    // Set the search path for dynamic modules.
    // The factory uses newDynamicModuleByName which looks up modules by name in this path.
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  void TearDown() override { TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH"); }

  DynamicModuleClusterFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
};

// Test StringValue config unpacking.
TEST_F(DynamicModuleClusterConfigTest, StringValueConfigUnpacking) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  // Use just the module name, not the full path. The factory uses newDynamicModuleByName
  // which constructs the path using ENVOY_DYNAMIC_MODULES_SEARCH_PATH.
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Pack a StringValue config.
  Protobuf::StringValue config_value;
  config_value.set_value("test_config_string_value");
  cluster_config.mutable_cluster_config()->PackFrom(config_value);

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value().first, nullptr);
  EXPECT_NE(result.value().second, nullptr);
}

// Test BytesValue config unpacking.
TEST_F(DynamicModuleClusterConfigTest, BytesValueConfigUnpacking) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Pack a BytesValue config.
  Protobuf::BytesValue config_value;
  config_value.set_value("binary_config_data_12345");
  cluster_config.mutable_cluster_config()->PackFrom(config_value);

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value().first, nullptr);
}

// Test Struct config unpacking (JSON serialization).
TEST_F(DynamicModuleClusterConfigTest, StructConfigUnpacking) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Pack a Struct config.
  Protobuf::Struct struct_config;
  (*struct_config.mutable_fields())["key1"].set_string_value("value1");
  (*struct_config.mutable_fields())["key2"].set_number_value(42);
  (*struct_config.mutable_fields())["key3"].set_bool_value(true);
  cluster_config.mutable_cluster_config()->PackFrom(struct_config);

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value().first, nullptr);
}

// Test unknown Any type (hash encoding fallback).
TEST_F(DynamicModuleClusterConfigTest, UnknownTypeHashEncoding) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Pack an unknown proto type (Duration) - not StringValue/BytesValue/Struct.
  Protobuf::Duration duration;
  duration.set_seconds(100);
  duration.set_nanos(500000);
  cluster_config.mutable_cluster_config()->PackFrom(duration);

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  // This should use hash encoding fallback.
  auto result = factory_.create(cluster_proto, context);
  EXPECT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value().first, nullptr);
}

// Test malformed StringValue (UnpackTo failure).
TEST_F(DynamicModuleClusterConfigTest, StringValueUnpackFailure) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Create malformed Any with StringValue type_url but invalid data.
  auto* any = cluster_config.mutable_cluster_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value("invalid_protobuf_bytes_xyz");

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to unpack StringValue"));
}

// Test malformed BytesValue (UnpackTo failure).
TEST_F(DynamicModuleClusterConfigTest, BytesValueUnpackFailure) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");

  // Create malformed Any with BytesValue type_url but invalid data.
  auto* any = cluster_config.mutable_cluster_config();
  any->set_type_url("type.googleapis.com/google.protobuf.BytesValue");
  any->set_value("invalid_protobuf_bytes_abc");

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to unpack BytesValue"));
}

// Test empty config (no cluster_config field).
TEST_F(DynamicModuleClusterConfigTest, EmptyConfig) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("cluster_no_op");
  // No cluster_config set.

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value().first, nullptr);
}

// Test invalid module name (module loading failure).
TEST_F(DynamicModuleClusterConfigTest, InvalidModuleName) {
  envoy::config::cluster::v3::Cluster cluster_proto;
  cluster_proto.set_name("test_cluster");
  cluster_proto.mutable_connect_timeout()->set_seconds(1);
  cluster_proto.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

  envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig cluster_config;
  cluster_config.set_cluster_name("test_cluster");
  cluster_config.mutable_dynamic_module_config()->set_name("nonexistent_module_xyz");

  cluster_proto.mutable_cluster_type()->set_name("envoy.clusters.dynamic_modules");
  cluster_proto.mutable_cluster_type()->mutable_typed_config()->PackFrom(cluster_config);

  Upstream::ClusterFactoryContextImpl context(server_context_, nullptr, nullptr, false);

  auto result = factory_.create(cluster_proto, context);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to load dynamic module"));
}

} // namespace
} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
