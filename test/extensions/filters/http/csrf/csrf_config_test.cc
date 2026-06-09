#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"

#include "source/extensions/filters/http/csrf/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

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

  auto cb = factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  EXPECT_NE(cb, nullptr);
}

} // namespace
} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
