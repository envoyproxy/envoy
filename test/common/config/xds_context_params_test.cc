#include "source/common/config/xds_context_params.h"

#include "test/common/config/xds_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds_test_utility.h"

using ::testing::Pair;

namespace Envoy {
namespace Config {
namespace {

// Validate all the node parameter renderers (except user_agent_build_version, which has its own
// test below).
TEST(XdsContextParamsTest, NodeAll) {
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
  const std::vector<std::string> params_vec{"id",
                                            "cluster",
                                            "user_agent_name",
                                            "user_agent_version",
                                            "locality.region",
                                            "locality.zone",
                                            "locality.sub_zone",
                                            "metadata"};
  Protobuf::RepeatedPtrField<std::string> node_context_params{params_vec.cbegin(),
                                                              params_vec.cend()};

  const auto context_params = XdsContextParams::encodeResource(
      XdsContextParams::encodeNodeContext(node, node_context_params), {}, {}, {});
  EXPECT_CONTEXT_PARAMS(
      context_params, Pair("xds.node.cluster", "some_cluster"), Pair("xds.node.id", "some_id"),
      Pair("xds.node.locality.sub_zone", "some_sub_zone"),
      Pair("xds.node.locality.zone", "some_zone"), Pair("xds.node.locality.region", "some_region"),
      Pair("xds.node.metadata.bar", "\"a\""), Pair("xds.node.metadata.baz", "42"),
      Pair("xds.node.metadata.foo", "true"), Pair("xds.node.user_agent_name", "xds_client"),
      Pair("xds.node.user_agent_version", "1.2.3"));
}

// Validate that we can select a subset of node parameters.
TEST(XdsContextParamsTest, NodeParameterSelection) {
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
  const std::vector<std::string> params_vec{"cluster", "user_agent_version", "locality.region",
                                            "locality.sub_zone"};
  Protobuf::RepeatedPtrField<std::string> node_context_params{params_vec.cbegin(),
                                                              params_vec.cend()};
  const auto context_params = XdsContextParams::encodeResource(
      XdsContextParams::encodeNodeContext(node, node_context_params), {}, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("xds.node.cluster", "some_cluster"),
                        Pair("xds.node.locality.sub_zone", "some_sub_zone"),
                        Pair("xds.node.locality.region", "some_region"),
                        Pair("xds.node.user_agent_version", "1.2.3"));
}

// Validate user_agent_build_version renderers.
TEST(XdsContextParamsTest, NodeUserAgentBuildVersion) {
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
  const std::vector<std::string> params_vec{"user_agent_build_version.version",
                                            "user_agent_build_version.metadata"};
  Protobuf::RepeatedPtrField<std::string> node_context_params{params_vec.cbegin(),
                                                              params_vec.cend()};
  const auto context_params = XdsContextParams::encodeResource(
      XdsContextParams::encodeNodeContext(node, node_context_params), {}, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params,
                        Pair("xds.node.user_agent_build_version.metadata.bar", "\"a\""),
                        Pair("xds.node.user_agent_build_version.metadata.baz", "42"),
                        Pair("xds.node.user_agent_build_version.metadata.foo", "true"),
                        Pair("xds.node.user_agent_build_version.version", "1.2.3"));
}

// Validate that resource locator context parameters are pass-thru.
TEST(XdsContextParamsTest, ResoureContextParams) {
  xds::core::v3::ContextParams resource_context_params;
  TestUtility::loadFromYaml(R"EOF(
  params:
    foo: "\"some_string\""
    bar: "123"
    baz: "true"
  )EOF",
                            resource_context_params);
  const auto context_params = XdsContextParams::encodeResource({}, resource_context_params, {}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("bar", "123"), Pair("baz", "true"),
                        Pair("foo", "\"some_string\""));
}

// Validate client feature capabilities context parameter transform.
TEST(XdsContextParamsTest, ClientFeatureCapabilities) {
  const auto context_params =
      XdsContextParams::encodeResource({}, {}, {"some.feature", "another.feature"}, {});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("xds.client_feature.another.feature", "true"),
                        Pair("xds.client_feature.some.feature", "true"));
}

// Validate per-resource well-known attributes transform.
TEST(XdsContextParamsTest, ResourceWktAttribs) {
  const auto context_params =
      XdsContextParams::encodeResource({}, {}, {}, {{"foo", "1"}, {"bar", "2"}});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("xds.resource.foo", "1"),
                        Pair("xds.resource.bar", "2"));
}

// Validate that the precedence relationships in the specification hold.
TEST(XdsContextParamsTest, Layering) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  id: some_id
  cluster: some_cluster
  )EOF",
                            node);
  xds::core::v3::ContextParams resource_context_params;
  TestUtility::loadFromYaml(R"EOF(
  params:
    id: another_id
    xds.node.cluster: another_cluster
  )EOF",
                            resource_context_params);
  const std::vector<std::string> params_vec{"id", "cluster"};
  Protobuf::RepeatedPtrField<std::string> node_context_params{params_vec.cbegin(),
                                                              params_vec.cend()};
  const auto context_params = XdsContextParams::encodeResource(
      XdsContextParams::encodeNodeContext(node, node_context_params), resource_context_params,
      {"id"}, {{"cluster", "huh"}});
  EXPECT_CONTEXT_PARAMS(context_params, Pair("id", "another_id"),
                        Pair("xds.client_feature.id", "true"),
                        Pair("xds.node.cluster", "another_cluster"), Pair("xds.node.id", "some_id"),
                        Pair("xds.resource.cluster", "huh"));
}

} // namespace
} // namespace Config
} // namespace Envoy
