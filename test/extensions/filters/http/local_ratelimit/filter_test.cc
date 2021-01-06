#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"

#include "extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"

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

  void setupPerRoute(const std::string& yaml, const bool enabled = true, const bool enforced = true,
                     const bool per_route = false) {
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
    config_ = std::make_shared<FilterConfig>(config, local_info_, dispatcher_, stats_, runtime_,
                                             per_route);
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }
  void setup(const std::string& yaml, const bool enabled = true, const bool enforced = true) {
    setupPerRoute(yaml, enabled, enforced);
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
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
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

static const std::string descriptor_config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: {}
  tokens_per_fill: 1
  fill_interval: 60s
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
descriptors:
- entries:
   - key: hello
     value: world
   - key: foo
     value: bar
  token_bucket:
    max_tokens: 10
    tokens_per_fill: 10
    fill_interval: 60s
- entries:
   - key: foo2
     value: bar2
  token_bucket:
    max_tokens: {}
    tokens_per_fill: 1
    fill_interval: 60s
stage: {}
  )";

class DescriptorFilterTest : public FilterTest {
public:
  DescriptorFilterTest() = default;

  void setUpTest(const std::string& yaml) {
    setupPerRoute(yaml, true, true, true);
    decoder_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
    decoder_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_
        .emplace_back(route_rate_limit_);
  }

  // Default token bucket
  RateLimit::TokenBucket bucket;
  std::vector<RateLimit::LocalDescriptor> descriptor_{{{{"foo2", "bar2"}}, {bucket}}};
  std::vector<RateLimit::LocalDescriptor> descriptor_first_match_{{
      {{
           {"hello", "world"},
           {"foo", "bar"},
       },
       {bucket}},
      {{{"foo2", "bar2"}}, {bucket}},
  }};
  std::vector<RateLimit::LocalDescriptor> descriptor_not_found_{{{{"foo", "bar"}}, {bucket}}};
  NiceMock<Router::MockRateLimitPolicyEntry> route_rate_limit_;
};

TEST_F(DescriptorFilterTest, NoRouteEntry) {
  setupPerRoute(fmt::format(descriptor_config_yaml, "1", "1", "0"), true, true, true);

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, NoCluster) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "1", "0"));

  EXPECT_CALL(decoder_callbacks_, clusterInfo()).WillRepeatedly(testing::Return(nullptr));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, DisabledInRoute) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  route_rate_limit_.disable_key_ = "disabled";

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorRequestOk) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateLocalDescriptors(_, _, _, _, _, _))
      .WillOnce(testing::SetArgReferee<1>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorRequestRatelimited) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateLocalDescriptors(_, _, _, _, _, _))
      .WillOnce(testing::SetArgReferee<1>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorNotFound) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateLocalDescriptors(_, _, _, _, _, _))
      .WillOnce(testing::SetArgReferee<1>(descriptor_not_found_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorFirstMatch) {
  // Request  should not be rate  limited as it should match first descriptor with 10 req/min
  setUpTest(fmt::format(descriptor_config_yaml, "0", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateLocalDescriptors(_, _, _, _, _, _))
      .WillOnce(testing::SetArgReferee<1>(descriptor_first_match_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorWithStageConfig) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "1", "1"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(1));

  EXPECT_CALL(route_rate_limit_, populateLocalDescriptors(_, _, _, _, _, _))
      .WillOnce(testing::SetArgReferee<1>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
