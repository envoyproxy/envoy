#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"

#include "source/extensions/filters/http/custom_response/config.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"
#include "source/extensions/filters/http/custom_response/factory.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/mocks.h"
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

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
