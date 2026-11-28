#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/http/fault/config.h"

#include "test/extensions/filters/http/fault/utility.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {
namespace {

TEST(FaultFilterConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort();
  EXPECT_THROW(FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context).value(),
               ProtoValidationException);
}

TEST(FaultFilterConfigTest, GrpcStatusResponseHeaderRejected) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  for (uint32_t grpc_status : {0, 5}) {
    for (const auto& header_name : {"grpc-status", "Grpc-Status", "GRPC-STATUS"}) {
      for (const auto& header_value : {"5", "14", "%RESPONSE_CODE%"}) {
        SCOPED_TRACE(grpc_status);
        SCOPED_TRACE(header_name);
        SCOPED_TRACE(header_value);
        envoy::extensions::filters::http::fault::v3::HTTPFault fault;
        fault.mutable_abort()->set_grpc_status(grpc_status);
        auto* header = fault.mutable_abort()->add_response_headers_to_add()->mutable_header();
        header->set_key(header_name);
        header->set_value(header_value);

        EXPECT_THROW_WITH_MESSAGE(
            FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context).value(),
            EnvoyException,
            "grpc-status cannot be set in response_headers_to_add when abort.grpc_status is "
            "configured");
      }
    }
  }
}

TEST(FaultFilterConfigTest, GrpcStatusResponseHeaderRejectedPerRoute) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort()->set_grpc_status(5);
  auto* header = fault.mutable_abort()->add_response_headers_to_add()->mutable_header();
  header->set_key("grpc-status");
  header->set_value("14");

  EXPECT_THROW_WITH_MESSAGE(
      FaultFilterFactory()
          .createRouteSpecificFilterConfig(fault, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value(),
      EnvoyException,
      "grpc-status cannot be set in response_headers_to_add when abort.grpc_status is configured");
}

TEST(FaultFilterConfigTest, GrpcStatusResponseHeaderAllowedWithoutGrpcStatus) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort()->set_http_status(503);
  auto* header = fault.mutable_abort()->add_response_headers_to_add()->mutable_header();
  header->set_key("grpc-status");
  header->set_value("14");

  EXPECT_NO_THROW(
      FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context).value());

  fault.mutable_abort()->mutable_header_abort();
  EXPECT_NO_THROW(
      FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context).value());
}

TEST(FaultFilterConfigTest, FaultFilterCorrectJson) {
  const std::string yaml_string = R"EOF(
  filter_metadata:
    hello: "world"
  delay:
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  )EOF";

  const auto proto_config = convertYamlStrToProtoConfig(yaml_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FaultFilterConfigTest, FaultFilterCorrectJsonWithServerContext) {
  const std::string yaml_string = R"EOF(
  filter_metadata:
    hello: "world"
  delay:
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  )EOF";

  envoy::extensions::filters::http::fault::v3::HTTPFault config;
  TestUtility::loadFromYamlAndValidate(yaml_string, config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  FaultFilterFactory factory;
  Server::Configuration::ExtraFactoryContext extra_context{context.messageValidationVisitor(),
                                                           "stats"};
  Http::FilterFactoryCb cb =
      factory.createHttpFilterFactoryFromProto(config, context, extra_context).value();
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

TEST(FaultFilterConfigTest, FaultFilterCorrectProto) {
  envoy::extensions::filters::http::fault::v3::HTTPFault config;
  config.mutable_delay()->mutable_percentage()->set_numerator(100);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(5);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FaultFilterConfigTest, FaultFilterEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context)
          .value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
