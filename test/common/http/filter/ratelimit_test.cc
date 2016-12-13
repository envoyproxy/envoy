#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/filter/ratelimit.h"
#include "common/http/headers.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::WithArgs;

namespace Http {
namespace RateLimit {

class HttpRateLimitFilterTest : public testing::Test {
public:
  HttpRateLimitFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_key.http_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  void SetUpTest(const std::string json) {
    Json::ObjectPtr config = Json::Factory::LoadFromString(json);
    config_.reset(new FilterConfig(*config, "service_cluster", stats_store_, runtime_));

    client_ = new ::RateLimit::MockClient();
    filter_.reset(new Filter(config_, ::RateLimit::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    rate_limit_policies_.clear();
    rate_limit_policies_.emplace_back(rate_limit_policy_entry_);
  }

  const std::string filter_config = R"EOF(
    {
      "domain": "foo",
    }
    )EOF";

  FilterConfigPtr config_;
  ::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  ::RateLimit::RequestCallbacks* request_callbacks_{};
  TestHeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::vector<std::reference_wrapper<Router::RateLimitPolicyEntry>> rate_limit_policies_;
  Router::TestRateLimitPolicyEntry rate_limit_policy_entry_;
};

TEST_F(HttpRateLimitFilterTest, NoRoute) {
  SetUpTest(filter_config);

  EXPECT_CALL(filter_callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoLimiting) {
  SetUpTest(filter_config);

  EXPECT_CALL(filter_callbacks_.route_table_.route_entry_.rate_limit_policy_,
              getApplicableRateLimit_(0)).WillOnce(testing::ReturnRef(rate_limit_policies_));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RuntimeDisabled) {
  SetUpTest(filter_config);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, OkResponse) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  std::vector<::RateLimit::Descriptor> descriptor{
      {{{"to_cluster", "fake_cluster"}}},
      {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}};
  EXPECT_CALL(filter_callbacks_.route_table_.route_entry_.rate_limit_policy_,
              getApplicableRateLimit_(0)).WillOnce(testing::ReturnRef(rate_limit_policies_));

  EXPECT_CALL(rate_limit_policy_entry_, populateDescriptors_(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor));

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}}),
                    "requestid"))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  request_headers_.addViaCopy(Http::Headers::get().RequestId, "requestid");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ImmediateOkResponse) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}}),
                    ""))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ErrorResponse) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  // EXPECT_CALL(filter_callbacks_.route_table_.route_entry_.rate_limit_policy_,
  // getApplicableRateLimit_(0)).WillOnce(testing::ReturnRef(empty_vector));
  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::Error);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.error").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponse) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  request_callbacks_->complete(::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_4xx").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponseRuntimeDisabled) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_4xx").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, ResetDuringCall) {
  SetUpTest(filter_config);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.reset_callback_();
}

TEST_F(HttpRateLimitFilterTest, RequestHeaderOkResponse) {
  SetUpTest(filter_config);

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foobar", testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                                     {{{"my_header_name", "test_value"}}}}),
                    ""))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  TestHeaderMapImpl request_header{{"x-header-name", "test_value"}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_header, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_header));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, RateLimitKeyOkResponse) {
  SetUpTest(filter_config);

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.route_key_ = "test_key";

  EXPECT_CALL(*client_, limit(_, "foobar",
                              testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                                  {{{"my_header_name", "test_value"}}},
                                  {{{"route_key", "test_key"}, {"my_header_name", "test_value"}}}}),
                              ""))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  TestHeaderMapImpl request_header{{"x-header-name", "test_value"}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_header, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_header));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, NoRateLimitHeaderMatch) {
  SetUpTest(filter_config);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RouteAddressRateLimiting) {
  SetUpTest(filter_config);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.route_key_ = "test_key";

  std::string address = "10.0.0.1";
  EXPECT_CALL(filter_callbacks_, downstreamAddress()).WillOnce(ReturnRef(address));
  EXPECT_CALL(*client_,
              limit(_, "foo", testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                                  {{{"remote_address", address}}},
                                  {{{"route_key", "test_key"}, {"remote_address", address}}}}),
                    ""))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, NoAddressRateLimiting) {
  SetUpTest(filter_config);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(filter_callbacks_, downstreamAddress()).WillOnce(ReturnRef(EMPTY_STRING));

  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RateLimitDisabledForRouteKey) {
  SetUpTest(filter_config);

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.route_key_ = "test_key";
  ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_key.http_filter_enabled", 100))
      .WillByDefault(Return(false));

  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

} // RateLimit
} // Http
