#include "common/common/empty_string.h"
#include "common/buffer/buffer_impl.h"
#include "common/http/filter/ratelimit.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::WithArgs;

namespace Http {
namespace RateLimit {

TEST(HttpRateLimitFilterBadConfigTest, BadType) {
  std::string json = R"EOF(
  {
    "domain": "foo",
    "actions": [
      {"type": "foo"}
    ]
  }
  )EOF";

  Json::StringLoader config(json);
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Runtime::MockLoader> runtime;
  EXPECT_THROW(FilterConfig(config, "service_cluster", stats_store, runtime), EnvoyException);
}

TEST(HttpRateLimitFilterBadConfigTest, NoDescriptorKey) {
  std::string json = R"EOF(
  {
    "domain": "foo",
    "actions": [
      {
        "type": "request_headers",
        "header_name" : "test"
      }
    ]
  }
  )EOF";

  Json::StringLoader config(json);
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Runtime::MockLoader> runtime;
  EXPECT_THROW(FilterConfig(config, "service_cluster", stats_store, runtime), EnvoyException);
}

class HttpRateLimitFilterTest : public testing::Test {
public:
  HttpRateLimitFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
        .WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
        .WillByDefault(Return(true));
  }

  void SetUpTest(const std::string json) {
    Json::StringLoader config(json);
    config_.reset(new FilterConfig(config, "service_cluster", stats_store_, runtime_));

    client_ = new ::RateLimit::MockClient();
    filter_.reset(new Filter(config_, ::RateLimit::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  const std::string service_to_service_json = R"EOF(
    {
      "domain": "foo",
      "actions": [
        {"type": "service_to_service"}
      ]
    }
    )EOF";

  const std::string request_headers_json = R"EOF(
    {
      "domain": "foobar",
      "actions": [
        {
          "type": "request_headers",
          "header_name": "x-header-name",
          "descriptor_key" : "my_header_name"
        }
      ]
    }
    )EOF";

  const std::string address_json = R"EOF(
    {
      "domain": "foo",
      "actions": [
        {"type": "remote_address"}
      ]
    }
    )EOF";

  FilterConfigPtr config_;
  ::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  ::RateLimit::RequestCallbacks* request_callbacks_{};
  HeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(HttpRateLimitFilterTest, NoRoute) {
  SetUpTest(service_to_service_json);

  EXPECT_CALL(filter_callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoLimiting) {
  SetUpTest(service_to_service_json);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RuntimeDisabled) {
  SetUpTest(service_to_service_json);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, OkResponse) {
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}})))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ImmediateOkResponse) {
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foo",
                    testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                        {{{"to_cluster", "fake_cluster"}}},
                        {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}})))
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
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
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
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Http::HeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  request_callbacks_->complete(::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.over_limit").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_4xx").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponseRuntimeDisabled) {
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
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
  SetUpTest(service_to_service_json);
  InSequence s;

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*client_, cancel());
  filter_callbacks_.reset_callback_();
}

TEST_F(HttpRateLimitFilterTest, RequestHeaderOkResponse) {
  SetUpTest(request_headers_json);

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_,
              limit(_, "foobar", testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                                     {{{"my_header_name", "test_value"}}}})))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  HeaderMapImpl request_header{{"x-header-name", "test_value"}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_header, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_header));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, RateLimitKeyOkResponse) {
  SetUpTest(request_headers_json);

  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.route_key_ = "test_key";

  EXPECT_CALL(
      *client_,
      limit(_, "foobar", testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                             {{{"my_header_name", "test_value"}}},
                             {{{"route_key", "test_key"}, {"my_header_name", "test_value"}}}})))
      .WillOnce(WithArgs<0>(Invoke([&](::RateLimit::RequestCallbacks& callbacks)
                                       -> void { request_callbacks_ = &callbacks; })));

  HeaderMapImpl request_header{{"x-header-name", "test_value"}};
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_header, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_header));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, NoRateLimitHeaderMatch) {
  SetUpTest(request_headers_json);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(*client_, limit(_, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, AddressRateLimiting) {
  SetUpTest(address_json);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  std::string address = "10.0.0.1";
  EXPECT_CALL(filter_callbacks_, address()).WillOnce(ReturnRef(address));
  EXPECT_CALL(*client_, limit(_, "foo", testing::ContainerEq(std::vector<::RateLimit::Descriptor>{
                                            {{{"remote_address", address}}}})))
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
  SetUpTest(address_json);
  filter_callbacks_.route_table_.route_entry_.rate_limit_policy_.do_global_limiting_ = true;

  EXPECT_CALL(filter_callbacks_, address()).WillOnce(ReturnRef(EMPTY_STRING));

  EXPECT_CALL(*client_, limit(_, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

} // RateLimit
} // Http
