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

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
namespace {

class CustomResponseFilterTest : public testing::Test {
public:
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

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

  std::unique_ptr<CustomResponseFilter> filter_;
  std::shared_ptr<FilterConfig> config_;
  Http::TestResponseHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(CustomResponseFilterTest, LocalData) {
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestRequestHeaderMapImpl request_headers{};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "499"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, RemoteData) {
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_CALL(decoder_callbacks_, recreateStream(_));
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, NoMatcherInConfig) {
  createConfig("{}");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "503"}};
  Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(CustomResponseFilterTest, MatchNotFound) {
  createConfig("{}");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(CustomResponseFilterTest, DontChangeStatusCode) {
  createConfig(R"EOF(
  custom_response_matcher:
    on_no_match:
      action:
        name: action
        typed_config:
          "@type": type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig
          name: local_response
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.custom_response.v3.CustomResponse.LocalResponsePolicy
            body:
              inline_string: "not allowed"
)EOF");
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  Http::TestRequestHeaderMapImpl request_headers{};
  EXPECT_EQ(filter_->decodeHeaders(request_headers, false), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(filter_->encodeHeaders(response_headers, true),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ("200", response_headers.getStatusValue());
}

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
