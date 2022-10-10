#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

constexpr absl::string_view kDefaultConfig = R"EOF(
  custom_response_matcher:
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
            name: custom_response1
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: local_response
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.LocalResponsePolicy
                status_code: 499
                body:
                  inline_string: "not allowed"
                body_format:
                  text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
                headers_to_add:
                - header:
                    key: "foo"
                    value: "x-bar"
                  append: false
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
            name: custom_response2
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: redirect_policy
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.RedirectPolicy
                status_code: 299
                uri: "https://foo.example/gateway_error"
                headers_to_add:
                - header:
                    key: "foo2"
                    value: "x-bar2"
                  append: false
      - predicate:
          single_predicate:
            input:
              name: status_code
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
            value_match:
              exact: "500"
        on_match:
          action:
            name: custom_response3
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: redirect_policy
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.RedirectPolicy
                status_code: 292
                uri: "https://foo.example/internal_server_error"
                headers_to_add:
                - header:
                    key: "foo3"
                    value: "x-bar3"
                  append: false
  )EOF";

}
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
