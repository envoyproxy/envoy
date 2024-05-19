#include "source/common/config/context_provider_impl.h"

#include "test/common/config/xds_test_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::Pair;

namespace Envoy {
namespace Config {
namespace {

TEST(ContextProviderTest, NodeContext) {
  envoy::config::core::v3::Node node;
  TestUtility::loadFromYaml(R"EOF(
  id: some_id
  cluster: some_cluster
  user_agent_name: xds_client
  user_agent_version: 1.2.3
  locality:
    region: some_region
  metadata:
    foo: true
  )EOF",
                            node);
  const std::vector<std::string> params_vec{
      "id",
      "cluster",
      "user_agent_name",
  };
  Protobuf::RepeatedPtrField<std::string> node_context_params{params_vec.cbegin(),
                                                              params_vec.cend()};
  ContextProviderImpl context_provider(node, node_context_params);
  EXPECT_CONTEXT_PARAMS(context_provider.nodeContext(), Pair("xds.node.cluster", "some_cluster"),
                        Pair("xds.node.id", "some_id"),
                        Pair("xds.node.user_agent_name", "xds_client"));
}

TEST(ContextProviderTest, DynamicContextParameters) {
  Protobuf::RepeatedPtrField<std::string> node_context_params;
  ContextProviderImpl context_provider({}, node_context_params);

  uint32_t update_count = 0;
  std::string last_updated_resource;
  auto callback_handle = context_provider.addDynamicContextUpdateCallback(
      [&update_count, &last_updated_resource](absl::string_view resource_type_url) {
        ++update_count;
        last_updated_resource = std::string(resource_type_url);
        return absl::OkStatus();
      });

  // Default empty DCP for all types.
  const auto& default_empty = context_provider.dynamicContext("unknown_type");
  EXPECT_EQ(0, default_empty.params().size());
  EXPECT_EQ(0, update_count);

  // Setting a DCP.
  context_provider.setDynamicContextParam("some_type", "foo", "bar");
  EXPECT_EQ(1, update_count);
  EXPECT_EQ("some_type", last_updated_resource);
  EXPECT_EQ("bar", context_provider.dynamicContext("some_type").params().at("foo"));

  // Updating a DCP.
  context_provider.setDynamicContextParam("some_type", "foo", "baz");
  EXPECT_EQ(2, update_count);
  EXPECT_EQ("some_type", last_updated_resource);
  EXPECT_EQ("baz", context_provider.dynamicContext("some_type").params().at("foo"));

  // Setting a DCP on an unrelated resource.
  context_provider.setDynamicContextParam("other_type", "foo", "bar");
  EXPECT_EQ(3, update_count);
  EXPECT_EQ("other_type", last_updated_resource);
  EXPECT_EQ("baz", context_provider.dynamicContext("some_type").params().at("foo"));
  EXPECT_EQ("bar", context_provider.dynamicContext("other_type").params().at("foo"));

  // Unsetting a DCP
  context_provider.unsetDynamicContextParam("some_type", "foo");
  EXPECT_EQ(4, update_count);
  EXPECT_EQ("some_type", last_updated_resource);
  EXPECT_EQ(0, context_provider.dynamicContext("some_type").params().count("foo"));
}

} // namespace
} // namespace Config
} // namespace Envoy
