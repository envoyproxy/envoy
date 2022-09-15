#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

constexpr absl::string_view kDefaultConfig = R"EOF(
  custom_responses:
  - name: 400_response
    status_code: 499
    local:
      body:
        inline_string: "not allowed"
      body_format:
        text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
    headers_to_add:
    - header:
        key: "foo"
        value: "x-bar"
      append: false
  - name: gateway_error_response
    status_code: 299
    remote:
      uri: "https://foo.example/gateway_error"
    headers_to_add:
    - header:
        key: "foo2"
        value: "x-bar2"
      append: false
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

}
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
