#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/extensions/filters/http/custom_response/config.h"
#include "source/extensions/filters/http/custom_response/factory.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

TEST(CustomResponseFilterConfigTest, CustomResponseFilter) {
  envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
  TestUtility::loadFromYaml(std::string(kDefaultConfig), filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  CustomResponseFilterFactory factory;
  ::Envoy::Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context);
  ::Envoy::Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(CustomResponseFilterConfigTest, InvalidURI) {
  envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
  std::string config(kDefaultConfig);
  config.replace(config.find("https"), 4, "%#?*");

  TestUtility::loadFromYaml(config, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  CustomResponseFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(filter_config, "stats", context),
                            EnvoyException,
                            "Invalid uri specified for redirection for custom response: "
                            "%#?*s://foo.example/gateway_error");
}

TEST(CustomResponseFilterConfigTest, NoHostAndPathRedirect) {
  envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
  std::string config(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
          redirect_action:
            https_redirect: true
          status_code: 292
)EOF");

  TestUtility::loadFromYaml(config, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  CustomResponseFilterFactory factory;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProto(filter_config, "stats", context), EnvoyException,
      "At least one of host_redirect and path_redirect needs to be specified");
}

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
