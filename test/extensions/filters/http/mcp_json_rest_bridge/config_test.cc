#include "source/extensions/filters/http/mcp_json_rest_bridge/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::testing::NiceMock;

TEST(McpJsonRestBridgeFilterConfigFactoryTest, RegisterAndCreateFilterWithEmptyConfig) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.mcp_json_rest_bridge");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  absl::StatusOr<Http::FilterFactoryCb> cb =
      factory->createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_TRUE(cb.ok());

  // TODO(paulhong01): Update the following verification once the proto config is processed
  // properly.
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter).Times(0);
  (*cb)(filter_callbacks);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
