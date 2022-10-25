#pragma once

#include "envoy/http/filter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/custom_response/policy.h"

#include "test/integration/filters/common.h"

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
                host: "https://foo.example"
                path: "/gateway_error"
                response_headers_to_add:
                - header:
                    key: "foo2"
                    value: "x-bar2"
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
              name: redirect_response2
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.RedirectPolicy
                status_code: 292
                host: "https://some.other.host"
                path: "/internal_server_error"
                response_headers_to_add:
                - header:
                    key: "foo3"
                    value: "x-bar3"
                request_headers_to_add:
                - header:
                    key: "cer-only"
                  keep_empty_value: true
  )EOF";

// Simulate filters that send local reply during either encode or decode based
// on route specific config.
class LocalReplyDuringDecodeIfNotCER : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-during-decode-if-not-cer";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {

    auto filter_state =
        decoder_callbacks_->streamInfo()
            .filterState()
            ->getDataReadOnly<Envoy::Extensions::HttpFilters::CustomResponse::Policy>(
                "envoy.filters.http.custom_response");
    if (!filter_state) {
      decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr,
                                         absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

class LocalReplyDuringEncodeIfNotCER : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-during-encode-if-not-cer";

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {

    auto filter_state = encoder_callbacks_->streamInfo()
                            .filterState()
                            ->getDataReadOnly<Extensions::HttpFilters::CustomResponse::Policy>(
                                "envoy.filters.http.custom_response");
    if (!filter_state) {
      encoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr,
                                         absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
