#include "common/config/udpa_context_params.h"

#include "test/common/config/udpa_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "udpa_test_utility.h"

using ::testing::Pair;

namespace Envoy {
namespace Config {
namespace {

// Validate all the node parameter renderers (except user_agent_build_version, which has its own
// test below).
TEST(UdpaContextParamsTest, NodeAll) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  id: some_id
  cluster: some_cluster
  user_agent_name: xds_client
  user_agent_version: 1.2.3
  locality:
    region: some_region
    zone: some_zone
    sub_zone: some_sub_zone
  metadata:
    foo: true
    bar: "a"
    baz: 42
  )EOF",
                            node);
  const auto context_params = UdpaContextParams::encode(
      node,
      {"id", "cluster", "user_agent_name", "user_agent_version", "locality.region", "locality.zone",
       "locality.sub_zone", "metadata"},
      {}, {}, {});
  EXPECT_CONTEXT_PARAMS(
      context_params, Pair("udpa.node.cluster", "some_cluster"), Pair("udpa.node.id", "some_id"),
      Pair("udpa.node.locality.sub_zone", "some_sub_zone"),
      Pair("udpa.node.locality.zone", "some_zone"),
      Pair("udpa.node.locality.region", "some_region"), Pair("udpa.node.metadata.bar", "\"a\""),
      Pair("udpa.node.metadata.baz", "42"), Pair("udpa.node.metadata.foo", "true"),
      Pair("udpa.node.user_agent_name", "xds_client"),
      Pair("udpa.node.user_agent_version", "1.2.3"));
}

// Validate that we can select a subset of node parameters.
TEST(UdpaContextParamsTest, NodeParameterSelection) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  id: some_id
  cluster: some_cluster
  user_agent_name: xds_client
  user_agent_version: 1.2.3
  locality:
    region: some_region
    zone: some_zone
    sub_zone: some_sub_zone
  metadata:
    foo: true
    bar: "a"
    baz: 42
  )EOF",
                            node);
  const auto context_params = UdpaContextParams::encode(
      node, {"cluster", "user_agent_version", "locality.region", "locality.sub_zone"}, {}, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("udpa.node.cluster", "some_cluster"),
                        Pair("udpa.node.locality.sub_zone", "some_sub_zone"),
                        Pair("udpa.node.locality.region", "some_region"),
                        Pair("udpa.node.user_agent_version", "1.2.3"));
}

// Validate user_agent_build_version renderers.
TEST(UdpaContextParamsTest, NodeUserAgentBuildVersion) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  user_agent_build_version:
    version:
      major_number: 1
      minor_number: 2
      patch: 3
    metadata:
      foo: true
      bar: "a"
      baz: 42
  )EOF",
                            node);
  const auto context_params = UdpaContextParams::encode(
      node, {"user_agent_build_version.version", "user_agent_build_version.metadata"}, {}, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params,
                        Pair("udpa.node.user_agent_build_version.metadata.bar", "\"a\""),
                        Pair("udpa.node.user_agent_build_version.metadata.baz", "42"),
                        Pair("udpa.node.user_agent_build_version.metadata.foo", "true"),
                        Pair("udpa.node.user_agent_build_version.version", "1.2.3"));
}

// Validate that resource locator context parameters are pass-thru.
TEST(UdpaContextParamsTest, ResoureContextParams) {
  udpa::core::v1::ContextParams resource_context_params;
  TestUtility::loadFromYaml(R"EOF(
  params:
    foo: "\"some_string\""
    bar: "123"
    baz: "true"
  )EOF",
                            resource_context_params);
  const auto context_params = UdpaContextParams::encode({}, {}, resource_context_params, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("bar", "123"), Pair("baz", "true"),
                        Pair("foo", "\"some_string\""));
}

// Validate client feature capabilities context parameter transform.
TEST(UdpaContextParamsTest, ClientFeatureCapabilities) {
  const auto context_params =
      UdpaContextParams::encode({}, {}, {}, {"some.feature", "another.feature"}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("udpa.client_feature.another.feature", "true"),
                        Pair("udpa.client_feature.some.feature", "true"));
}

// Validate per-resource well-known attributes transform.
TEST(UdpaContextParamsTest, ResourceWktAttribs) {
  const auto context_params =
      UdpaContextParams::encode({}, {}, {}, {}, {{"foo", "1"}, {"bar", "2"}});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("udpa.resource.foo", "1"),
                        Pair("udpa.resource.bar", "2"));
}

// Validate that the precedence relationships in the specification hold.
TEST(UdpaContextParamsTest, Layering) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  id: some_id
  cluster: some_cluster
  )EOF",
                            node);
  udpa::core::v1::ContextParams resource_context_params;
  TestUtility::loadFromYaml(R"EOF(
  params:
    id: another_id
    udpa.node.cluster: another_cluster
  )EOF",
                            resource_context_params);
  const auto context_params = UdpaContextParams::encode(
      node, {"id", "cluster"}, resource_context_params, {"id"}, {{"cluster", "huh"}});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("id", "another_id"),
                        Pair("udpa.client_feature.id", "true"),
                        Pair("udpa.node.cluster", "another_cluster"),
                        Pair("udpa.node.id", "some_id"), Pair("udpa.resource.cluster", "huh"));
}

} // namespace
} // namespace Config
} // namespace Envoy
