#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"

#include "source/common/singleton/manager_impl.h"
#include "source/extensions/filters/http/local_ratelimit/local_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

static constexpr absl::string_view config_yaml = R"(
stat_prefix: test
rate_limited_as_resource_exhausted: {}
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
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
  - header:
      key: test-resp-req-id
      value: '%REQ(test-req-id)%'
request_headers_to_add_when_not_enforced:
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-local-ratelimited
      value: 'true'
local_rate_limit_per_downstream_connection: {}
enable_x_ratelimit_headers: {}
enable_retry_after_header: {}
status:
  code: {}
  )";
// '{}' used in the yaml config above are position dependent placeholders used for substitutions.
// Different test cases toggle functionality based on these positional placeholder variables
// For instance, fmt::format(config_yaml, "1", "false") substitutes '1' and 'false' for 'max_tokens'
// and 'local_rate_limit_per_downstream_connection' configurations, respectively.

static constexpr absl::string_view simple_config_yaml_without_enforce = R"(
  stat_prefix: test
  token_bucket:
    max_tokens: 1
    tokens_per_fill: 1
    fill_interval: 1000s
  filter_enabled:
    runtime_key: test_enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
)";

static constexpr absl::string_view custom_retry_after_config_yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 1
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
  - header:
      key: retry-after
      value: '120'
enable_retry_after_header: true
)";

class FilterTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  FilterTest() = default;

  void setupPerRoute(const std::string& yaml, const bool enabled = true, const bool enforced = true,
                     const bool per_route = false, const bool has_enabled = true,
                     const bool has_enforced = true) {
    if (has_enabled) {
      EXPECT_CALL(
          factory_context_.runtime_loader_.snapshot_,
          featureEnabled(absl::string_view("test_enabled"),
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
          .WillRepeatedly(testing::Return(enabled));
    } else {
      ASSERT(!enabled); // No filter_enabled and this should be false always.
    }

    if (has_enforced) {
      EXPECT_CALL(
          factory_context_.runtime_loader_.snapshot_,
          featureEnabled(absl::string_view("test_enforced"),
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
          .WillRepeatedly(testing::Return(enforced));
    } else {
      ASSERT(!enforced); // No filter_enforced and this should be false always.
    }

    ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(decoder_callbacks_2_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));

    envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit config;
    TestUtility::loadFromYaml(yaml, config);
    absl::Status creation_status = absl::OkStatus();
    config_ = std::make_shared<FilterConfig>(config, factory_context_, *stats_.rootScope(),
                                             creation_status, per_route);
    ASSERT_TRUE(creation_status.ok()) << creation_status.message();
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);

    filter_2_ = std::make_shared<Filter>(config_);
    filter_2_->setDecoderFilterCallbacks(decoder_callbacks_2_);
  }
  void setup(const std::string& yaml, const bool enabled = true, const bool enforced = true) {
    setupPerRoute(yaml, enabled, enforced);
  }

  void expectRemaining(uint64_t remaining) {
    ASSERT_NE(nullptr, filter_->token_bucket_context_);
    EXPECT_EQ(remaining, filter_->token_bucket_context_->remainingTokens());
  }

  void expectRemainingAfterRequest(absl::Span<const RateLimit::Descriptor> descriptors,
                                   uint64_t remaining) {
    const auto result = filter_->requestAllowed(descriptors);
    EXPECT_TRUE(result.allowed);
    ASSERT_NE(nullptr, result.token_bucket_context);
    EXPECT_EQ(remaining, result.token_bucket_context->remainingTokens());
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  Http::Code toErrorCode(const uint64_t code) { return config_->toErrorCode(code); }

  Stats::IsolatedStoreImpl stats_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_2_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
  std::shared_ptr<Filter> filter_2_;
};

TEST_F(FilterTest, Runtime) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"), false, false);
  EXPECT_EQ(&factory_context_.runtime_loader_, &(config_->runtime()));
}

TEST_F(FilterTest, ToErrorCode) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"), false, false);
  EXPECT_EQ(Http::Code::BadRequest, toErrorCode(400));
}

TEST_F(FilterTest, Disabled) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"), false, false);
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
}

TEST_F(FilterTest, NoEnforced) {
  setupPerRoute(std::string(simple_config_yaml_without_enforce), true, false, false, true, false);
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
}

TEST_F(FilterTest, RequestOk) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_2_->decodeHeaders(headers, false));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestOkPerConnection) {
  setup(fmt::format(config_yaml, "false", "1", "true", "\"OFF\"", "false", "429"));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->decodeHeaders(headers, false));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimited) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus> grpc_status,
                          absl::string_view details) {
        EXPECT_EQ(Http::Code::TooManyRequests, code);
        EXPECT_EQ("local_rate_limited", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
        modify_headers(response_headers);
        EXPECT_EQ("true", response_headers.get(Http::LowerCaseString("x-test-rate-limit"))[0]
                              ->value()
                              .getStringView());
        // Make sure that generated local reply headers contain a value dynamically
        // generated by header formatter REQ(test-req-id)
        EXPECT_EQ("123", response_headers.get(Http::LowerCaseString("test-resp-req-id"))[0]
                             ->value()
                             .getStringView());
        EXPECT_EQ(grpc_status, std::nullopt);
        EXPECT_EQ(details, "local_rate_limited");
      }));

  // Add a custom header to the request.
  // Locally generated reply is configured to refer to this value.
  Http::TestRequestHeaderMapImpl request_headers{{"test-req-id", "123"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(decoder_callbacks_2_, streamInfo).WillRepeatedly(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, getRequestHeaders).WillRepeatedly(Return(&request_headers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimitedResourceExhausted) {
  setup(fmt::format(config_yaml, "true", "1", "false", "\"OFF\"", "false", "429"));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus> grpc_status,
                          absl::string_view details) {
        EXPECT_EQ(Http::Code::TooManyRequests, code);
        EXPECT_EQ("local_rate_limited", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
        modify_headers(response_headers);
        EXPECT_EQ("true", response_headers.get(Http::LowerCaseString("x-test-rate-limit"))[0]
                              ->value()
                              .getStringView());
        // Make sure that generated local reply headers contain a value dynamically
        // generated by header formatter REQ(test-req-id)
        EXPECT_EQ("123", response_headers.get(Http::LowerCaseString("test-resp-req-id"))[0]
                             ->value()
                             .getStringView());
        EXPECT_EQ(grpc_status,
                  std::make_optional(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted));
        EXPECT_EQ(details, "local_rate_limited");
      }));

  // Add a custom header to the request.
  // Locally generated reply is configured to refer to this value.
  Http::TestRequestHeaderMapImpl request_headers{{"test-req-id", "123"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(decoder_callbacks_2_, streamInfo).WillRepeatedly(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, getRequestHeaders).WillRepeatedly(Return(&request_headers));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

/*
This test sets 'local_rate_limit_per_downstream_connection' to true. Doing this enables per
connection rate limiting and even though 'max_token' is set to 1, it allows 2 requests to go through
- one on each connection. This is in contrast to the 'RequestOk' test above where only 1 request is
allowed (across the process) for the same configuration.
*/
TEST_F(FilterTest, RequestRateLimitedPerConnection) {
  setup(fmt::format(config_yaml, "false", "1", "true", "\"OFF\"", "false", "429"));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus> grpc_status,
                          absl::string_view details) {
        EXPECT_EQ(Http::Code::TooManyRequests, code);
        EXPECT_EQ("local_rate_limited", body);

        Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
        modify_headers(response_headers);
        EXPECT_EQ("true", response_headers.get(Http::LowerCaseString("x-test-rate-limit"))[0]
                              ->value()
                              .getStringView());

        EXPECT_EQ(grpc_status, std::nullopt);
        EXPECT_EQ(details, "local_rate_limited");
      }));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto expected_headers = Http::TestRequestHeaderMapImpl();

  EXPECT_EQ(request_headers, expected_headers);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
  EXPECT_EQ(4U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimitedButNotEnforced) {
  setup(fmt::format(config_yaml, "false", "0", "false", "\"OFF\"", "false", "429"), true, false);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _)).Times(0);

  auto request_headers = Http::TestRequestHeaderMapImpl();
  Http::TestRequestHeaderMapImpl expected_headers{
      {"x-local-ratelimited", "true"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(request_headers, expected_headers);
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimitedXRateLimitHeaders) {
  setup(fmt::format(config_yaml, "false", "1", "false", "DRAFT_VERSION_03", "false", "429"));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ("1", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-remaining"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->encodeHeaders(response_headers, false));
  EXPECT_EQ("1", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-remaining"));
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(FilterTest, RequestRateLimitedXRateLimitHeadersWithoutRunningDecodeHeaders) {
  setup(fmt::format(config_yaml, "false", "1", "false", "DRAFT_VERSION_03", "false", "429"));

  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("", response_headers.get_("x-ratelimit-remaining"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->encodeHeaders(response_headers, false));
  EXPECT_EQ("", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("", response_headers.get_("x-ratelimit-remaining"));
}

static constexpr absl::string_view descriptor_config_yaml = R"(
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
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
local_rate_limit_per_downstream_connection: true
enable_x_ratelimit_headers: {}
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

static constexpr absl::string_view inlined_descriptor_config_yaml = R"(
stat_prefix: test
token_bucket:
  # Big global token bucket to ensure that the request
  # is not rate limited by the global token bucket.
  max_tokens: 100000
  tokens_per_fill: 100000
  fill_interval: 1s
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
descriptors:
- entries:
   - key: foo2
     value: bar2
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
rate_limits:
- actions:
  - header_value_match:
      descriptor_key: foo2
      descriptor_value: bar2
      headers:
      - name: x-header-name
        string_match:
          exact: test_value
  )";

static constexpr absl::string_view inlined_descriptor_config_yaml_with_custom_hits_addend = R"(
stat_prefix: test
token_bucket:
  # Big global token bucket to ensure that the request
  # is not rate limited by the global token bucket.
  max_tokens: 100000
  tokens_per_fill: 100000
  fill_interval: 1s
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
descriptors:
- entries:
   - key: foo2
     value: bar2
  token_bucket:
    max_tokens: {}
    tokens_per_fill: {}
    fill_interval: {}
rate_limits:
- actions:
  - header_value_match:
      descriptor_key: foo2
      descriptor_value: bar2
      headers:
      - name: x-header-name
        string_match:
          exact: test_value_1
  hits_addend:
    format: "%BYTES_RECEIVED%"
- actions:
  - header_value_match:
      descriptor_key: foo2
      descriptor_value: bar2
      headers:
      - name: x-header-name
        string_match:
          exact: test_value_2
  hits_addend:
    number: 5
enable_retry_after_header: {}
  )";

static constexpr absl::string_view consume_default_token_config_yaml = R"(
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
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
local_rate_limit_per_downstream_connection: true
always_consume_default_token_bucket: {}
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

static constexpr absl::string_view descriptor_vh_config_yaml = R"(
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
  - append_action: OVERWRITE_IF_EXISTS_OR_ADD
    header:
      key: x-test-rate-limit
      value: 'true'
local_rate_limit_per_downstream_connection: true
enable_x_ratelimit_headers: {}
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
vh_rate_limits: {}
  )";

class DescriptorFilterTest : public FilterTest {
public:
  DescriptorFilterTest() = default;

  void setUpTest(const std::string& yaml) {
    setupPerRoute(yaml, true, true, true);
    decoder_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
    decoder_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_
        .emplace_back(route_rate_limit_);
    decoder_callbacks_.route_->virtual_host_->rate_limit_policy_.rate_limit_policy_entry_.clear();
    decoder_callbacks_.route_->virtual_host_->rate_limit_policy_.rate_limit_policy_entry_
        .emplace_back(vh_rate_limit_);
  }

  std::vector<RateLimit::Descriptor> descriptor_{{{{"foo2", "bar2"}}}};
  std::vector<RateLimit::Descriptor> descriptor_first_match_{{
      {{
          {"hello", "world"},
          {"foo", "bar"},
      }},
      {{{"foo2", "bar2"}}},
  }};
  std::vector<RateLimit::Descriptor> descriptor_not_found_{{{{"foo", "bar"}}}};
  NiceMock<Router::MockRateLimitPolicyEntry> route_rate_limit_;
  NiceMock<Router::MockRateLimitPolicyEntry> vh_rate_limit_;
};

TEST_F(DescriptorFilterTest, NoRouteEntry) {
  setupPerRoute(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"), true, true, true);

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, NoCluster) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"));

  decoder_callbacks_.cluster_info_ = nullptr;

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, DisabledInRoute) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"));

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
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorRequestRatelimited) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "\"OFF\"", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorNotFound) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_not_found_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorNotFoundWithConsumeDefaultTokenTrue) {
  setUpTest(fmt::format(consume_default_token_config_yaml, "0", "true", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_not_found_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorWithConsumeDefaultTokenTrue) {
  setUpTest(fmt::format(consume_default_token_config_yaml, "0", "true", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorWithConsumeDefaultTokenFalse) {
  setUpTest(fmt::format(consume_default_token_config_yaml, "0", "false", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorNotFoundWithConsumeDefaultTokenFalse) {
  setUpTest(fmt::format(consume_default_token_config_yaml, "0", "false", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_not_found_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorBothMatch) {
  // Request should also be rate limited as it should match both descriptors and global token.
  setUpTest(fmt::format(descriptor_config_yaml, "0", "\"OFF\"", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_first_match_));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorWithStageConfig) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "1"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(1));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorRequestRatelimitedXRateLimitHeaders) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "DRAFT_VERSION_03", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-remaining"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest,
       RouteDescriptorRequestRatelimitedXRateLimitHeadersButDescriptorDisabled) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "DRAFT_VERSION_03", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));
  descriptor_[0].x_ratelimit_option_ = envoy::config::route::v3::RateLimit::OFF;
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("x-ratelimit-limit")).empty());
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("x-ratelimit-remaining")).empty());
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, RouteDescriptorRequestRatelimitedWithoutXRateLimitHeaders) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "\"OFF\"", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("x-ratelimit-limit")).empty());
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("x-ratelimit-remaining")).empty());
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest,
       RouteDescriptorRequestRatelimitedWithoutXRateLimitHeadersButDescriptorEnabled) {
  setUpTest(fmt::format(descriptor_config_yaml, "0", "\"OFF\"", "0", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));
  descriptor_[0].x_ratelimit_option_ = envoy::config::route::v3::RateLimit::DRAFT_VERSION_03;
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  auto request_headers = Http::TestRequestHeaderMapImpl();
  auto response_headers = Http::TestResponseHeaderMapImpl();

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-limit"));
  EXPECT_EQ("0", response_headers.get_("x-ratelimit-remaining"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enabled"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.rate_limited"));
}

TEST_F(DescriptorFilterTest, NoVHRateLimitOption) {
  setUpTest(fmt::format(descriptor_config_yaml, "1", "\"OFF\"", "1", "0"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_, empty())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(0);
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the route rate limit is used when VhRateLimitsOptions::OVERRIDE and route rate limit
// is set
TEST_F(DescriptorFilterTest, OverrideVHRateLimitOptionWithRouteRateLimitSet) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "OVERRIDE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_, empty())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(0);
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the virtual host rate limit is used when VhRateLimitsOptions::OVERRIDE is set and
// route rate limit is empty
TEST_F(DescriptorFilterTest, OverrideVHRateLimitOptionWithoutRouteRateLimit) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "OVERRIDE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_, empty())
      .WillOnce(Return(true));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the virtual host rate limit is used when VhRateLimitsOptions::INCLUDE is set and route
// rate limit is empty
TEST_F(DescriptorFilterTest, IncludeVHRateLimitOptionWithOnlyVHRateLimitSet) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "INCLUDE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the virtual host rate limit is used when VhRateLimitsOptions::INCLUDE and route rate
// limit is set
TEST_F(DescriptorFilterTest, IncludeVHRateLimitOptionWithRouteAndVHRateLimitSet) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "INCLUDE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the route rate limit is used when VhRateLimitsOptions::IGNORE and route rate limit is
// set
TEST_F(DescriptorFilterTest, IgnoreVHRateLimitOptionWithRouteRateLimitSet) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "IGNORE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(0);

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that no rate limit is used when VhRateLimitsOptions::IGNORE is set and route rate limit
// empty
TEST_F(DescriptorFilterTest, IgnoreVHRateLimitOptionWithOutRouteRateLimit) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "IGNORE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(0);

  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Tests that the virtual host rate limit is used when includeVirtualHostRateLimits is used
TEST_F(DescriptorFilterTest, IncludeVirtualHostRateLimitsSetTrue) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "1", "\"OFF\"", "1", "0", "IGNORE"));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(decoder_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(true));

  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(0));

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  auto headers = Http::TestRequestHeaderMapImpl();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

TEST_F(DescriptorFilterTest, UseInlinedRateLimitConfig) {
  setUpTest(fmt::format(inlined_descriptor_config_yaml, 1, 1, "60s"));

  auto headers = Http::TestRequestHeaderMapImpl();
  // Requests will not be blocked because the requests don't match any descriptor and
  // the global token bucket has enough tokens.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

  headers.setCopy(Http::LowerCaseString("x-header-name"), "test_value");

  // Only one request is allowed in 60s for the matched request.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(DescriptorFilterTest, InlinedRequestOnlyActionsSkipCompletionWork) {
  setUpTest(fmt::format(inlined_descriptor_config_yaml, 2, 1, "60s"));
  EXPECT_TRUE(config_->hasRateLimitConfigs());
  EXPECT_FALSE(config_->hasStreamDoneActions());
  Http::TestRequestHeaderMapImpl headers{{"x-header-name", "test_value"}};
  EXPECT_CALL(decoder_callbacks_, routeSharedPtr()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  expectRemaining(1);

  // Completion skips stream info lookup and route-policy traversal.
  EXPECT_CALL(decoder_callbacks_, streamInfo()).Times(0);
  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(_))
      .Times(0);
  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(_))
      .Times(0);
  filter_->onDestroy();
  expectRemaining(1);
}

TEST_F(DescriptorFilterTest, UseInlinedRateLimitConfigWithCustomHitsAddend) {
  setUpTest(
      fmt::format(inlined_descriptor_config_yaml_with_custom_hits_addend, 119, 119, "60s", false));

  auto headers = Http::TestRequestHeaderMapImpl();

  // Requests will not be blocked because the requests don't match any descriptor and
  // the global token bucket has enough tokens.
  for (size_t i = 0; i < 120; i++) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  }

  decoder_callbacks_.stream_info_.bytes_received_ = 100;

  {
    headers.setCopy(Http::LowerCaseString("x-header-name"), "test_value_1");

    // 119 -> 19.
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  }

  {
    headers.setCopy(Http::LowerCaseString("x-header-name"), "test_value_2");

    // 19 -> 14.
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    // 14 -> 9.
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    // 9 -> 4.
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

    // No enough tokens.
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
  }
}

TEST_F(DescriptorFilterTest, RetryAfterHeaderClampsZeroResetToOne) {
  setUpTest(fmt::format(inlined_descriptor_config_yaml_with_custom_hits_addend, 1, 1, "60s", true));

  auto headers = Http::TestRequestHeaderMapImpl{{"x-header-name", "test_value_2"}};

  // The bucket has one token, but this request consumes five. The request is rejected while
  // nextTokenAvailable() is zero because a token is already available.
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        EXPECT_EQ("1", response_headers.get_("retry-after"));
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(FilterTest, RequestRateLimitedWithRetryAfterHeader) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "true", "429"));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        // The bucket refills one token every 1000 seconds, so that is when the next token is
        // available.
        EXPECT_EQ("1000", response_headers.get_("retry-after"));
      }));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
}

TEST_F(FilterTest, RequestRateLimitedPreservesCustomRetryAfterHeader) {
  setup(std::string(custom_retry_after_config_yaml));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        EXPECT_EQ("120", response_headers.get_("retry-after"));
      }));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
}

TEST_F(FilterTest, RequestRateLimitedWithoutRetryAfterHeaderByDefault) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "false", "429"));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        EXPECT_TRUE(response_headers.get(Http::LowerCaseString("retry-after")).empty());
      }));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
}

TEST_F(FilterTest, RequestAllowedWithoutRetryAfterHeader) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "true", "429"));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("retry-after")).empty());
}

TEST_F(FilterTest, RequestRateLimitedWithoutRetryAfterHeaderWhenNotEnforced) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "true", "429"), true, false);

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _))
      .Times(0);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->decodeHeaders(request_headers, false));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.get(Http::LowerCaseString("retry-after")).empty());
}

TEST_F(FilterTest, RequestRateLimitedWithoutRetryAfterHeaderForNon429Status) {
  setup(fmt::format(config_yaml, "false", "1", "false", "\"OFF\"", "true", "503"));

  EXPECT_CALL(decoder_callbacks_2_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                          const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        EXPECT_TRUE(response_headers.get(Http::LowerCaseString("retry-after")).empty());
      }));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, false));
}

TEST_F(FilterTest, StreamDoneWithoutDescriptorsDoesNotConsumeDefaultBucket) {
  setup(fmt::format(config_yaml, "false", "2", "false", "\"OFF\"", "false", "429"));
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  filter_->onDestroy();
  expectRemaining(1);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->decodeHeaders(headers, true));
  filter_2_->onDestroy();
  expectRemaining(0);
}

class StreamDoneFilterTest : public FilterTest, public testing::WithParamInterface<bool> {
public:
  void setupStreamDone(const std::string& format = "%RESP(x-cost)%", bool enabled = true,
                       bool refund = false) {
    envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit config;
    TestUtility::loadFromYaml(fmt::format(inlined_descriptor_config_yaml, 6, 1, "1000s"), config);
    config.mutable_token_bucket()->set_max_tokens(2);
    config.set_local_rate_limit_per_downstream_connection(GetParam());
    config.mutable_rate_limits()->Clear();
    auto* request_limit = config.add_rate_limits();
    auto* action = request_limit->add_actions()->mutable_generic_key();
    action->set_descriptor_key("foo2");
    action->set_descriptor_value("bar2");
    auto* completion_limit = config.add_rate_limits();
    *completion_limit = *request_limit;
    completion_limit->set_apply_on_stream_done(true);
    completion_limit->mutable_hits_addend()->set_format(format);
    completion_limit->mutable_hits_addend()->set_is_negative_hits(refund);
    setup(MessageUtil::getJsonStringFromMessageOrError(config), enabled);
  }
};

INSTANTIATE_TEST_SUITE_P(PerConnection, StreamDoneFilterTest, testing::Bool());

TEST_P(StreamDoneFilterTest, ChargesResponseUsageAtCompletion) {
  setupStreamDone();
  EXPECT_TRUE(config_->hasStreamDoneActions());
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"x-cost", "5"}};
  EXPECT_CALL(decoder_callbacks_, routeSharedPtr());
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  // Encoding the response has not charged completion usage.
  expectRemaining(5);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  filter_->onDestroy();
  expectRemaining(0);
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enforced"));

  // The next stream enforces the updated process or connection budget.
  auto next_filter = std::make_shared<Filter>(config_);
  next_filter->setDecoderFilterCallbacks(decoder_callbacks_);
  testing::Mock::VerifyAndClearExpectations(&decoder_callbacks_);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            next_filter->decodeHeaders(request_headers, true));
}

TEST_P(StreamDoneFilterTest, CompletionUsesPerRouteConfig) {
  setupStreamDone();
  auto per_route_config = config_;
  setup(fmt::format(config_yaml, "false", "100", "false", "\"OFF\"", "false", "429"));
  EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
      .WillOnce(Return(per_route_config.get()));
  EXPECT_CALL(decoder_callbacks_, routeSharedPtr());
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"x-cost", "5"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->onDestroy();
  expectRemaining(0);
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.ok"));
}

TEST_P(StreamDoneFilterTest, CompletionCanRefundUsage) {
  setupStreamDone("%RESP(x-cost)%", true, true);
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"x-cost", "2"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  expectRemaining(5);
  filter_->onDestroy();
  expectRemaining(6);
}

TEST_P(StreamDoneFilterTest, UsesFinalStreamInfoWithoutResponseHeaders) {
  setupStreamDone("%BYTES_SENT%");
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  decoder_callbacks_.stream_info_.bytes_sent_ = 4;
  filter_->onDestroy();
  expectRemaining(1);
}

TEST_P(StreamDoneFilterTest, ChargesStreamInfoUsageForLocallyRateLimitedRequest) {
  setupStreamDone("%BYTES_SENT%");
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  filter_->onDestroy();

  Filter second_filter(config_);
  second_filter.setDecoderFilterCallbacks(decoder_callbacks_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            second_filter.decodeHeaders(request_headers, true));
  second_filter.onDestroy();

  // The default bucket is empty; descriptor tokens remain. Reuse callbacks so all three streams
  // share the connection bucket too.
  filter_2_->setDecoderFilterCallbacks(decoder_callbacks_);
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_2_->decodeHeaders(request_headers, true));
  expectRemaining(3);

  Http::TestResponseHeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_2_->encodeHeaders(response_headers, true));
  decoder_callbacks_.stream_info_.bytes_sent_ = 2;
  filter_2_->onDestroy();
  // Like global rate limiting, completion charges local-response usage.
  expectRemaining(1);
  EXPECT_EQ(2U, findCounter("test.http_local_rate_limit.ok"));
  EXPECT_EQ(1U, findCounter("test.http_local_rate_limit.enforced"));
}

TEST_P(StreamDoneFilterTest, InvalidResponseCostDoesNotChargeAgain) {
  setupStreamDone();
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"x-cost", "invalid"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->onDestroy();
  expectRemaining(5);
  if (!GetParam()) {
    EXPECT_TRUE(config_->requestAllowed({}).allowed);
    EXPECT_FALSE(config_->requestAllowed({}).allowed);
  }
}

TEST_P(StreamDoneFilterTest, MissingResponseCostDoesNotChargeAgain) {
  setupStreamDone();
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_LOG_NOT_CONTAINS("warning", "Invalid hits_addend", filter_->onDestroy());
  expectRemaining(5);
}

TEST_P(StreamDoneFilterTest, EmptyCompletionUsageDoesNotSkipLaterPolicy) {
  envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit config;
  TestUtility::loadFromYaml(fmt::format(inlined_descriptor_config_yaml, 6, 1, "1000s"), config);
  config.set_local_rate_limit_per_downstream_connection(GetParam());
  config.clear_rate_limits();
  for (const auto* format : {"%RESP(x-empty-cost)%", "%RESP(x-cost)%"}) {
    auto* policy = config.add_rate_limits();
    auto* action = policy->add_actions()->mutable_generic_key();
    action->set_descriptor_key("foo2");
    action->set_descriptor_value("bar2");
    policy->set_apply_on_stream_done(true);
    policy->mutable_hits_addend()->set_format(format);
  }
  setup(MessageUtil::getJsonStringFromMessageOrError(config));

  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{"x-empty-cost", ""}, {"x-cost", "3"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_LOG_NOT_CONTAINS("warning", "Invalid hits_addend", filter_->onDestroy());

  // The valid policy charges three tokens; a one-token request leaves two.
  RateLimit::Descriptor descriptor;
  descriptor.entries_ = {{"foo2", "bar2"}};
  expectRemainingAfterRequest({&descriptor, 1}, 2);
}

TEST_P(StreamDoneFilterTest, CompletionUsesRequestHeadersAtStreamDone) {
  static constexpr absl::string_view yaml = R"(
stat_prefix: test
token_bucket:
  max_tokens: 100
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
local_rate_limit_per_downstream_connection: {}
descriptors:
- entries:
  - key: tenant
    value: public
  token_bucket:
    max_tokens: 6
    tokens_per_fill: 1
    fill_interval: 1000s
- entries:
  - key: tenant
    value: internal
  token_bucket:
    max_tokens: 6
    tokens_per_fill: 1
    fill_interval: 1000s
rate_limits:
- actions:
  - request_headers:
      header_name: x-tenant
      descriptor_key: tenant
- actions:
  - request_headers:
      header_name: x-tenant
      descriptor_key: tenant
  apply_on_stream_done: true
  hits_addend:
    format: "%RESP(x-cost)%"
)";
  setup(fmt::format(yaml, GetParam()));
  RateLimit::Descriptor public_descriptor;
  public_descriptor.entries_ = {{"tenant", "public"}};
  RateLimit::Descriptor internal_descriptor;
  internal_descriptor.entries_ = {{"tenant", "internal"}};
  Http::TestRequestHeaderMapImpl request_headers{{"x-tenant", "public"}};
  Http::TestResponseHeaderMapImpl response_headers{{"x-cost", "3"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  expectRemaining(5);
  // A later filter or route rewrites the identity header before completion.
  request_headers.setCopy(Http::LowerCaseString("x-tenant"), "internal");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->onDestroy();
  // As in global rate limiting, completion charges the descriptor derived at stream completion.
  expectRemainingAfterRequest({&public_descriptor, 1}, 4);
  expectRemainingAfterRequest({&internal_descriptor, 1}, 2);

  // Removing the identity header leaves completion with no descriptor to charge.
  Filter next_filter(config_);
  next_filter.setDecoderFilterCallbacks(decoder_callbacks_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, next_filter.decodeHeaders(request_headers, true));
  request_headers.remove(Http::LowerCaseString("x-tenant"));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, next_filter.encodeHeaders(response_headers, true));
  next_filter.onDestroy();
  expectRemainingAfterRequest({&internal_descriptor, 1}, 0);
}

TEST_P(StreamDoneFilterTest, DisabledOrNotDecodedDoesNotAccount) {
  setupStreamDone();
  filter_->onDestroy();
  setupStreamDone("%RESP(x-cost)%", false);
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_CALL(decoder_callbacks_, routeSharedPtr()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  filter_->onDestroy();
  EXPECT_EQ(0U, findCounter("test.http_local_rate_limit.enabled"));
}

TEST_F(DescriptorFilterTest, RouteAndVirtualHostIgnoreApplyOnStreamDone) {
  setUpTest(fmt::format(descriptor_vh_config_yaml, "5", "\"OFF\"", "5", "0", "INCLUDE"));
  ON_CALL(route_rate_limit_, applyOnStreamDone()).WillByDefault(Return(true));
  ON_CALL(vh_rate_limit_, applyOnStreamDone()).WillByDefault(Return(true));
  // Route and virtual-host entries apply on the request path regardless of apply_on_stream_done.
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _))
      .WillOnce(testing::SetArgReferee<0>(descriptor_));
  EXPECT_CALL(decoder_callbacks_, routeSharedPtr()).Times(0);
  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
  expectRemaining(4);

  // Completion does not traverse route or virtual-host policies again.
  EXPECT_CALL(decoder_callbacks_, streamInfo()).Times(0);
  EXPECT_CALL(decoder_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(_))
      .Times(0);
  EXPECT_CALL(decoder_callbacks_.route_->virtual_host_->rate_limit_policy_,
              getApplicableRateLimit(_))
      .Times(0);
  filter_->onDestroy();
  expectRemaining(4);
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
