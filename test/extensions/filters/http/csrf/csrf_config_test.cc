#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"

#include "source/extensions/filters/http/csrf/config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::StatusHelpers::IsOkAndHolds;
using testing::NiceMock;
using testing::NotNull;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {
namespace {

TEST(CsrfFilterConfigTest, ServerContextOnlyFactory) {
  const std::string yaml_string = R"EOF(
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  )EOF";

  envoy::extensions::filters::http::csrf::v3::CsrfPolicy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CsrfFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto cb = factory.createHttpFilterFactoryFromProto(proto_config, "stats", context).value();
  EXPECT_NE(cb, nullptr);

  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(CsrfFilterConfigTest, RouteSpecificConfig) {
  const std::string yaml_string = R"EOF(
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  shadow_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  )EOF";

  envoy::extensions::filters::http::csrf::v3::CsrfPolicy proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  CsrfFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;

  auto result = factory.createRouteSpecificFilterConfig(proto_config, context, validation_visitor);
  ASSERT_THAT(result, IsOkAndHolds(NotNull()));
}

} // namespace
} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
