#include "source/extensions/filters/http/mcp/config.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

using ::Envoy::StatusHelpers::IsOkAndHolds;
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
  ASSERT_OK(cb);

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

  EXPECT_THAT(config_or, IsOkAndHolds(::testing::NotNull()));
}

TEST_F(McpFilterConfigTest, MaxRequestBodySizeAboveTenMegabytesIsAccepted) {
  constexpr uint32_t kLargeBodySize = 64 * 1024 * 1024;
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(kLargeBodySize);
  ASSERT_OK(factory_->createFilterFactoryFromProto(proto_config, "stats", context_));

  envoy::extensions::filters::http::mcp::v3::McpOverride override_config;
  override_config.mutable_max_request_body_size()->set_value(kLargeBodySize);
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  auto route_config_or = factory_->createRouteSpecificFilterConfig(
      override_config, server_context, ProtobufMessage::getNullValidationVisitor());
  ASSERT_OK(route_config_or);
  EXPECT_EQ(kLargeBodySize, dynamic_cast<const McpOverrideConfig&>(*route_config_or.value())
                                .maxRequestBodySize()
                                .value());
}

// Test creating filter with server context
TEST_F(McpFilterConfigTest, CreateFilterWithServerContext) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  Http::FilterFactoryCb cb =
      factory_->createHttpFilterFactoryFromProto(proto_config, server_context, extra_context)
          .value();

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
