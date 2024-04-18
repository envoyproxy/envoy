#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/http/kill_request/kill_request_config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {
namespace {

using testing::_;

TEST(KillRequestConfigTest, KillRequestFilterWithCorrectProto) {
  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request;
  kill_request.mutable_probability()->set_numerator(100);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  KillRequestFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(kill_request, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(KillRequestConfigTest, KillRequestFilterWithEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  KillRequestFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context)
          .value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(KillRequestConfigTest, RouteSpecificConfig) {
  KillRequestFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(route_config.get());
}

} // namespace
} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
