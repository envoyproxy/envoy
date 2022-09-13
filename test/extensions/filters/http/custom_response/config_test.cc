#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/extensions/filters/http/custom_response/config.h"
#include "source/extensions/filters/http/custom_response/factory.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

namespace {

TEST(CustomResponseFilterConfigTest, CustomResponseFilter) {
  std::string filter_config_yaml = R"EOF(
  custom_responses:
  - name: 400_response
    status_code: 401
    local:
      inline_string: "not allowed"
    headers_to_add:
    - header:
        key: "foo"
        value: "x-bar"
      append: false
    body_format:
      text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
  - name: gateway_error_response
    remote:
      http_uri:
        uri: "https://www.foo.com/gateway_error"
        cluster: "foo"
        timeout:
          seconds: 2
    body_format:
      text_format: "<h1>%LOCAL_REPLY_BODY% %REQ(:path)%</h1>"
      content_type: "text/html; charset=UTF-8"
  custom_response_matcher:
    matcher_tree:
      # First filter by server name.
      input:
        name: server_name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.ServerNameInput
      exact_match_map:
        map:
          server1.example.com:
            matcher:
              matcher_list:
                matchers:
                  # Apply the 400_response policy to any 4xx response
                - predicate:
                    single_predicate:
                      input:
                        name: status_code
                        typed_config:
                          "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeClassMatchInput
                      value_match:
                        exact: "4xx"
                  on_match:
                    action:
                      name: custom_response
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: 400_response
                  # Apply the gateway_error_response policy to status codes 502, 503 and 504.
                - predicate:
                    or_matcher:
                      predicate:
                      - single_predicate:
                          input:
                            name: status_code
                            typed_config:
                              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                          value_match:
                            exact: "502"
                      - single_predicate:
                          input:
                            name: status_code
                            typed_config:
                              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                          value_match:
                            exact: "503"
                      - single_predicate:
                          input:
                            name: status_code
                            typed_config:
                              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                          value_match:
                            exact: "504"
                  on_match:
                    action:
                      name: custom_response
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: gateway_error_response
  )EOF";
  envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  CustomResponseFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
