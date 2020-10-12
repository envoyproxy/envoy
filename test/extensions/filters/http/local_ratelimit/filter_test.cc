#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"

#include "extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

static const std::string config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: {}
  tokens_per_fill: 1
  fill_interval: 1000s
filter_enabled:
  runtime_key: test_enabled
  default_value:
    numerator: 100
    denominator: HUNDRED
filter_enforced:
  runtime_key: test_enforced
  default_value:
    numerator: 100
    denominator: HUNDRED
response_headers_to_add:
  - append: false
    header:
      key: x-test-rate-limit
      value: 'true'
  )";

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  void setup(const std::string& yaml, const bool enabled = true, const bool enforced = true) {
    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled(absl::string_view("test_enabled"),
                       testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
        .WillRepeatedly(testing::Return(enabled));
    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled(absl::string_view("test_enforced"),
                       testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
        .WillRepeatedly(testing::Return(enforced));

    envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, dispatcher_, stats_, runtime_);
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  Http::Code toErrorCode(const uint64_t code) { return config_->toErrorCode(code); }

  Stats::IsolatedStoreImpl stats_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
};

TEST_F(FilterTest, Runtime) {
  setup(fmt::format(config_yaml, "1"), false, false);
  EXPECT_EQ(&runtime_, &(config_->runtime()));
}

TEST_F(FilterTest, ToErrorCode) {
  setup(fmt::format(config_yaml, "1"), false, false);
  EXPECT_EQ(Http::Code::BadRequest, toErrorCode(400));
}

TEST_F(FilterTest, Disabled) {
  setup(fmt::format(config_yaml, "1"), false, false);
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
}

TEST_F(FilterTest, RequestOk) {
  setup(fmt::format(config_yaml, "1"));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(FilterTest, RequestRateLimited) {
  setup(fmt::format(config_yaml, "0"));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                          absl::string_view details) {
        EXPECT_EQ(Http::Code::TooManyRequests, code);
        EXPECT_EQ("local_rate_limited", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
        modify_headers(response_headers);
        EXPECT_EQ("true", response_headers.get(Http::LowerCaseString("x-test-rate-limit"))[0]
                              ->value()
                              .getStringView());

        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "local_rate_limited");
      }));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimitedButNotEnforced) {
  setup(fmt::format(config_yaml, "0"), true, false);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _)).Times(0);

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
