#include "source/extensions/filters/http/mcp/config.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;

class McpFilterConfigTest : public testing::Test {
public:
  void SetUp() override { factory_ = std::make_unique<McpFilterConfigFactory>(); }

protected:
  std::unique_ptr<McpFilterConfigFactory> factory_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Runtime::MockLoader> runtime_;
};

// Test creating filter from empty config
TEST_F(McpFilterConfigTest, CreateFilterWithEmptyConfig) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;

  absl::StatusOr<Http::FilterFactoryCb> cb =
      factory_->createFilterFactoryFromProto(proto_config, "stats", context_);
  ASSERT_TRUE(cb.ok());

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  (*cb)(filter_callbacks);
}

// Test creating route-specific config
TEST_F(McpFilterConfigTest, CreateRouteSpecificConfig) {
  envoy::extensions::filters::http::mcp::v3::McpOverride proto_config;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  auto config_or = factory_->createRouteSpecificFilterConfig(
      proto_config, server_context, ProtobufMessage::getNullValidationVisitor());

  EXPECT_TRUE(config_or.ok());
  EXPECT_NE(nullptr, config_or.value());
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
