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
        # Apply a locally stored custom response to any 4xx response.
      - predicate:
          single_predicate:
            input:
              name: 4xx_response
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeClassMatchInput
            value_match:
              exact: "4xx"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: local_response
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.LocalResponsePolicy
                status_code: 499
                body:
                  inline_string: "not allowed"
                body_format:
                  json_format:
                    status: "%RESPONSE_CODE%"
                    message: "%LOCAL_REPLY_BODY%"
                response_headers_to_add:
                - header:
                    key: "foo"
                    value: "x-bar"
                  append: false
        # Redirect to different upstream if the status code is one of 502, 503 or 504.
      - predicate:
          or_matcher:
            predicate:
            - single_predicate:
                input:
                  name: "502_response"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "502"
            - single_predicate:
                input:
                  name: "503_response"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "503"
            - single_predicate:
                input:
                  name: "504_response"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "504"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: redirect_response
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.RedirectPolicy
                status_code: 299
                uri: "https://foo.example/gateway_error"
                response_headers_to_add:
                - header:
                    key: "foo2"
                    value: "x-bar2"
                  append: false
      - predicate:
          single_predicate:
            input:
              name: "500_response"
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
            value_match:
              exact: "500"
        on_match:
          action:
            name: action
            typed_config:
              "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
              name: redirect_response
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.RedirectPolicy
                status_code: 292
                uri: "https://foo.example/internal_server_error"
                response_headers_to_add:
                - header:
                    key: "foo3"
                    value: "x-bar3"
                  append: false
  )EOF";

}
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
