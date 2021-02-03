#include "common/config/context_provider_impl.h"

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

} // namespace
} // namespace Config
} // namespace Envoy
