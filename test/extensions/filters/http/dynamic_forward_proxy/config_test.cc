#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {
namespace {

// The downstream HTTP filter path builds the filter factory from a FactoryContext.
TEST(DynamicForwardProxyFilterFactoryTest, FactoryContext) {
  DynamicForwardProxyFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

// The route/vhost-level HTTP filter path builds the filter factory directly from a
// ServerFactoryContext.
TEST(DynamicForwardProxyFilterFactoryTest, ServerFactoryContext) {
  DynamicForwardProxyFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig proto_config;
  Http::FilterFactoryCb cb =
      factory.createHttpFilterFactoryFromProto(proto_config, "stats", context).value();

  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(_));
  cb(filter_callbacks);
}

TEST(DynamicForwardProxyFilterFactoryTest, RouteSpecificConfig) {
  DynamicForwardProxyFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  EXPECT_TRUE(route_config.get());
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
