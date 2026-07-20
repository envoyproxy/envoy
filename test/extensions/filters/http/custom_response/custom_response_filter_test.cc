#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"

#include "source/extensions/filters/http/custom_response/config.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"
#include "source/extensions/filters/http/custom_response/factory.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "utility.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
namespace {

using LocalResponsePolicyProto =
    envoy::extensions::http::custom_response::local_response_policy::v3::LocalResponsePolicy;
using RedirectPolicyProto =
    envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy;

class CustomResponseFilterTest : public testing::Test {
public:
  void SetUp() override {
    EXPECT_CALL(context_, scope()).WillRepeatedly(::testing::ReturnRef(scope_));
  }

  void setupFilterAndCallback() {
    filter_ = std::make_unique<CustomResponseFilter>(config_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void createConfig(const absl::string_view config_str = kDefaultConfig) {
    envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
    TestUtility::loadFromYaml(std::string(config_str), filter_config);
    Stats::StatNameManagedStorage prefix("stats", context_.scope().symbolTable());
    config_ = std::make_shared<FilterConfig>(filter_config, context_, prefix.statName());
  }

  void setServerName(const std::string& server_name) {
    encoder_callbacks_.stream_info_.downstream_connection_info_provider_->setRequestedServerName(
        server_name);
  }

  Stats::TestUtil::TestStore stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<::Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<::Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;

  std::unique_ptr<CustomResponseFilter> filter_;
  std::shared_ptr<FilterConfig> config_;
  ::Envoy::Http::TestResponseHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(CustomResponseFilterTest, LocalData) {
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "401"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(static_cast<::Envoy::Http::Code>(499), "not allowed", _, _, _));
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, RemoteData) {
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, NoMatcherInConfig) {
  createConfig("{}");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
}

TEST_F(CustomResponseFilterTest, MatchNotFound) {
  createConfig("{}");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
}

// Matcher selecting a local response policy based on a request header value.
constexpr absl::string_view kRequestHeaderMatchConfig = R"EOF(
  custom_response_matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: accept_header
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: accept
            value_match:
              exact: "application/json"
        on_match:
          action:
            name: json_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy
              status_code: 498
              body:
                inline_string: "json error"
)EOF";

// A custom response policy is selected when the request header matches.
TEST_F(CustomResponseFilterTest, MatchRequestHeader) {
  createConfig(kRequestHeaderMatchConfig);
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/json"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(static_cast<::Envoy::Http::Code>(498), "json error", _, _, _));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
}

// No custom response policy is selected when the request header does not match;
// the original response is passed through unchanged.
TEST_F(CustomResponseFilterTest, RequestHeaderDoesNotMatch) {
  createConfig(kRequestHeaderMatchConfig);
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"accept", "text/html"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ("500", response_headers.getStatusValue());
}

// When request headers are not available (stream_info.getRequestHeaders() is nullptr), a matcher
// that relies on a request header input must not match (and must not crash); the original response
// is passed through unchanged.
TEST_F(CustomResponseFilterTest, NoRequestHeaders) {
  createConfig(kRequestHeaderMatchConfig);
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/json"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "500"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  // Deliberately do not set ON_CALL for getRequestHeaders(); the NiceMock default returns nullptr,
  // exercising the request-headers-absent branch in FilterConfig::getPolicy.
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ("500", response_headers.getStatusValue());
}

// Matcher selecting a redirect policy based on a request header value.
constexpr absl::string_view kRequestHeaderRedirectConfig = R"EOF(
  custom_response_matcher:
    matcher_list:
      matchers:
      - predicate:
          single_predicate:
            input:
              name: accept_header
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: accept
            value_match:
              exact: "application/json"
        on_match:
          action:
            name: redirect_action
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
              status_code: 299
              uri: "https://foo.example/gateway_error"
)EOF";

// A redirect policy is selected (recreating the stream) when the request header matches.
TEST_F(CustomResponseFilterTest, MatchRequestHeaderRedirect) {
  createConfig(kRequestHeaderRedirectConfig);
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/json"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_CALL(decoder_callbacks_, recreateStream(_));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
}

// The redirect policy is not selected (no stream recreation) when the request header does not
// match; the original response is passed through unchanged.
TEST_F(CustomResponseFilterTest, RequestHeaderRedirectNoMatch) {
  createConfig(kRequestHeaderRedirectConfig);
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"accept", "text/html"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_CALL(decoder_callbacks_, recreateStream(_)).Times(0);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ("503", response_headers.getStatusValue());
}

TEST_F(CustomResponseFilterTest, DontChangeStatusCode) {
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy
          body:
            inline_string: "not allowed"
)EOF");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ("200", response_headers.getStatusValue());
}

TEST_F(CustomResponseFilterTest, TraceIdInBodyFormat) {
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy
          status_code: 503
          body_format:
            text_format_source:
              inline_string: "trace=%TRACE_ID%"
)EOF");
  setupFilterAndCallback();

  const std::string trace_id = "abc123trace";
  ON_CALL(encoder_callbacks_.active_span_, getTraceId()).WillByDefault(Return(trace_id));

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(static_cast<::Envoy::Http::Code>(503), "trace=abc123trace", _, _, _));
  ON_CALL(encoder_callbacks_.stream_info_, getRequestHeaders())
      .WillByDefault(Return(&request_headers));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, InvalidHostRedirect) {
  // Create config with invalid host_redirect field
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
          redirect_action:
            host_redirect: "global_storage"
          status_code: 292
)EOF");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{};
  // Verify Continue was called, i.e. the redirect policy becomes a no-op.
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  // Verify we get the original response
  EXPECT_EQ("200", response_headers.getStatusValue());
  EXPECT_EQ(
      1U,
      stats_store_.findCounterByString("stats.custom_response_invalid_uri").value().get().value());
}

TEST_F(CustomResponseFilterTest, InvalidSchemeRedirect) {
  // Create config with invalid scheme field.
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
          redirect_action:
            scheme_redirect: x&#$
            path_redirect: "/abc"
          status_code: 292
)EOF");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"Host", "example.foo"}};
  // Verify Continue was called, i.e. the redirect policy becomes a no-op.
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  // Verify we get the original response
  EXPECT_EQ("200", response_headers.getStatusValue());
  EXPECT_EQ(
      1U,
      stats_store_.findCounterByString("stats.custom_response_invalid_uri").value().get().value());
}

TEST_F(CustomResponseFilterTest, PathRewrite) {
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
          redirect_action:
            host_redirect: "redirect.example.com"
            path_rewrite: "/new/%REQ(x-version)%"
          status_code: 302
)EOF");
  setupFilterAndCallback();

  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                          {":path", "/original"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"x-version", "v2"}};
  ::Envoy::Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false),
            ::Envoy::Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            ::Envoy::Http::FilterHeadersStatus::StopIteration);
  // Verify the path was set to the formatted value.
  EXPECT_EQ("/new/v2", request_headers.getPathValue());
  EXPECT_EQ("redirect.example.com", request_headers.getHostValue());
}

TEST_F(CustomResponseFilterTest, PathRewriteInvalid) {
  EXPECT_THROW_WITH_REGEX(createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.custom_response.redirect_policy.v3.RedirectPolicy
          redirect_action:
            host_redirect: "redirect.example.com"
            path_rewrite: "/new/%INVALID_COMMAND_WITH_NO_CLOSING"
)EOF"),
                          EnvoyException, "Failed to create path_rewrite formatter");
}

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
