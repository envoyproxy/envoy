#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/custom_response/policies/redirect_policy.h"
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
            name: 4xx_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.LocalResponsePolicy
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
            name: gateway_error_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.RedirectPolicy
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
            name: 500_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.RedirectPolicy
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
      - predicate:
          single_predicate:
            input:
              name: "520_response"
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
            value_match:
              exact: "520"
        on_match:
          action:
            name: 520_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.RedirectPolicy
              host: "https://global/storage"
              path: "/internal_server_error"
              response_headers_to_add:
              - header:
                  key: "foo3"
                  value: "x-bar3"
  )EOF";

// Helper methods and classes to modify the custom response config for tests.
template <typename Policy> inline const char* getTypeUrlHelper();

template <typename Policy> struct Traits {
  using ModifyPolicyFn = std::function<void(Policy&)>;
  static const char* getTypeUrl() { return getTypeUrlHelper<Policy>(); }
};

template <>
inline const char*
getTypeUrlHelper<envoy::extensions::filters::http::custom_response::v3::RedirectPolicy>() {
  return "type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.RedirectPolicy";
}

template <>
inline const char*
getTypeUrlHelper<envoy::extensions::filters::http::custom_response::v3::LocalResponsePolicy>() {
  return "type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.LocalResponsePolicy";
}

template <typename Policy>
void modifyPolicy(
    envoy::extensions::filters::http::custom_response::v3::CustomResponse& custom_response,
    absl::string_view name, typename Traits<Policy>::ModifyPolicyFn function) {
  for (auto& matcher : *custom_response.mutable_custom_response_matcher()
                            ->mutable_matcher_list()
                            ->mutable_matchers()) {
    auto& action = *matcher.mutable_on_match()->mutable_action();
    if (action.typed_config().type_url() == Traits<Policy>::getTypeUrl() && action.name() == name) {
      auto& any = *action.mutable_typed_config();
      Policy policy;
      any.UnpackTo(&policy);
      function(policy);
      any.PackFrom(policy);
    }
  }
}

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

class TestModifyRequestHeadersAction : public ModifyRequestHeadersAction {
public:
  ~TestModifyRequestHeadersAction() override = default;

  void modifyRequestHeaders(Envoy::Http::RequestHeaderMap& headers, Envoy::StreamInfo::StreamInfo&,
                            const RedirectPolicy& redirect_policy) override {
    headers.setCopy(Http::LowerCaseString("x-envoy-cer-backend"), redirect_policy.host().substr(8));
  };
};

class TestModifyRequestHeadersActionFactory : public ModifyRequestHeadersActionFactory {
public:
  ~TestModifyRequestHeadersActionFactory() override = default;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "modify-request-headers-action"; }

  std::unique_ptr<ModifyRequestHeadersAction>
  createAction(const Protobuf::Message&,
               Envoy::Server::Configuration::ServerFactoryContext&) override {
    return std::make_unique<TestModifyRequestHeadersAction>();
  }
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
