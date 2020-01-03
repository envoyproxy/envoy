#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/config/filter/http/fault/v2/fault.pb.validate.h"
#include "envoy/type/percent.pb.h"

#include "extensions/filters/http/fault/config.h"

#include "test/extensions/filters/http/fault/utility.h"
#include "test/mocks/server/mocks.h"

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
  envoy::config::filter::http::fault::v2::HTTPFault fault;
  fault.mutable_abort();
  EXPECT_THROW(FaultFilterFactory().createFilterFactoryFromProto(fault, "stats", context),
               ProtoValidationException);
}

TEST(FaultFilterConfigTest, FaultFilterCorrectJson) {
  const std::string yaml_string = R"EOF(
  delay:
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  )EOF";

  const auto proto_config = convertYamlStrToProtoConfig(yaml_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FaultFilterConfigTest, FaultFilterCorrectProto) {
  envoy::config::filter::http::fault::v2::HTTPFault config;
  config.mutable_delay()->mutable_percentage()->set_numerator(100);
  config.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::FractionalPercent::HUNDRED);
  config.mutable_delay()->mutable_fixed_delay()->set_seconds(5);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(FaultFilterConfigTest, FaultFilterEmptyProto) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  FaultFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
