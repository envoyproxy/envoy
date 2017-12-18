#include <memory>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/config/filter_json.h"
#include "common/http/filter/ratelimit.h"
#include "common/http/headers.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::WithArgs;
using testing::_;

namespace Envoy {
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
    Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
    envoy::api::v2::filter::http::RateLimit proto_config{};
    Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config);
    config_.reset(new FilterConfig(proto_config, local_info_, stats_store_, runtime_, cm_));

    client_ = new Envoy::RateLimit::MockClient();
    filter_.reset(new Filter(config_, Envoy::RateLimit::ClientPtr{client_}));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
    filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
    filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.emplace_back(
        route_rate_limit_);
    filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_.rate_limit_policy_entry_
        .clear();
    filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_.rate_limit_policy_entry_
        .emplace_back(vh_rate_limit_);
  }

  const std::string filter_config_ = R"EOF(
  {
    "domain": "foo"
  }
  )EOF";

  FilterConfigSharedPtr config_;
  Envoy::RateLimit::MockClient* client_;
  std::unique_ptr<Filter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Envoy::RateLimit::RequestCallbacks* request_callbacks_{};
  TestHeaderMapImpl request_headers_;
  Buffer::OwnedImpl data_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Router::MockRateLimitPolicyEntry> route_rate_limit_;
  NiceMock<Router::MockRateLimitPolicyEntry> vh_rate_limit_;
  std::vector<Envoy::RateLimit::Descriptor> descriptor_{{{{"descriptor_key", "descriptor_value"}}}};
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST_F(HttpRateLimitFilterTest, BadConfig) {
  const std::string filter_config = R"EOF(
  {
    "domain": "foo",
    "route_key" : "my_route"
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(filter_config);
  envoy::api::v2::filter::http::RateLimit proto_config{};
  EXPECT_THROW(Config::FilterJson::translateHttpRateLimitFilter(*json_config, proto_config),
               Json::Exception);
}

TEST_F(HttpRateLimitFilterTest, NoRoute) {
  SetUpTest(filter_config_);

  EXPECT_CALL(*filter_callbacks_.route_, routeEntry()).WillOnce(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoCluster) {
  SetUpTest(filter_config_);

  ON_CALL(cm_, get(_)).WillByDefault(Return(nullptr));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoApplicableRateLimit) {
  SetUpTest(filter_config_);

  filter_callbacks_.route_->route_entry_.rate_limit_policy_.rate_limit_policy_entry_.clear();
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, NoDescriptor) {
  SetUpTest(filter_config_);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(1);
  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(1);
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, RuntimeDisabled) {
  SetUpTest(filter_config_);

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, OkResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  request_headers_.addCopy(Http::Headers::get().RequestId, "requestid");
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(RequestInfo::ResponseFlag::RateLimited))
      .Times(0);
  request_callbacks_->complete(Envoy::RateLimit::LimitStatus::OK);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ImmediateOkResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(Envoy::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ErrorResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(Envoy::RateLimit::LimitStatus::Error);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(RequestInfo::ResponseFlag::RateLimited))
      .Times(0);

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.error").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponse) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl response_headers{{":status", "429"}};
  EXPECT_CALL(filter_callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), true));
  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(filter_callbacks_.request_info_,
              setResponseFlag(RequestInfo::ResponseFlag::RateLimited));

  request_callbacks_->complete(Envoy::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.over_limit")
                .value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, LimitResponseRuntimeDisabled) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ratelimit.http_filter_enforcing", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  request_callbacks_->complete(Envoy::RateLimit::LimitStatus::OverLimit);

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.over_limit")
                .value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_4xx").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_429").value());
}

TEST_F(HttpRateLimitFilterTest, ResetDuringCall) {
  SetUpTest(filter_config_);
  InSequence s;

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));
  EXPECT_CALL(*client_, limit(_, _, _, _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        request_callbacks_ = &callbacks;
      })));

  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(*client_, cancel());
  filter_->onDestroy();
}

TEST_F(HttpRateLimitFilterTest, RouteRateLimitDisabledForRouteKey) {
  route_rate_limit_.disable_key_ = "test_key";
  SetUpTest(filter_config_);

  ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_key.http_filter_enabled", 100))
      .WillByDefault(Return(false));

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, VirtualHostRateLimitDisabledForRouteKey) {
  vh_rate_limit_.disable_key_ = "test_vh_key";
  SetUpTest(filter_config_);

  ON_CALL(runtime_.snapshot_, featureEnabled("ratelimit.test_vh_key.http_filter_enabled", 100))
      .WillByDefault(Return(false));

  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));
}

TEST_F(HttpRateLimitFilterTest, IncorrectRequestType) {
  std::string internal_filter_config = R"EOF(
  {
    "domain": "foo",
    "request_type" : "internal"
  }
  )EOF";
  SetUpTest(internal_filter_config);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  std::string external_filter_config = R"EOF(
  {
    "domain": "foo",
    "request_type" : "external"
  }
  )EOF";
  SetUpTest(external_filter_config);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(vh_rate_limit_, populateDescriptors(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*client_, limit(_, _, _, _)).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(HttpRateLimitFilterTest, InternalRequestType) {
  std::string internal_filter_config = R"EOF(
  {
    "domain": "foo",
    "request_type" : "internal"
  }
  )EOF";
  SetUpTest(internal_filter_config);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};
  InSequence s;

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(Envoy::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ExternalRequestType) {

  std::string external_filter_config = R"EOF(
  {
    "domain": "foo",
    "request_type" : "external"
  }
  )EOF";
  SetUpTest(external_filter_config);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "false"}};
  InSequence s;

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(1);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(Envoy::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ExcludeVirtualHost) {
  std::string external_filter_config = R"EOF(
  {
    "domain": "foo"
  }
  )EOF";
  SetUpTest(external_filter_config);
  InSequence s;

  EXPECT_CALL(filter_callbacks_.route_->route_entry_.rate_limit_policy_, getApplicableRateLimit(0));
  EXPECT_CALL(route_rate_limit_, populateDescriptors(_, _, _, _, _))
      .WillOnce(SetArgReferee<1>(descriptor_));

  EXPECT_CALL(filter_callbacks_.route_->route_entry_, includeVirtualHostRateLimits())
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.route_->route_entry_.virtual_host_.rate_limit_policy_,
              getApplicableRateLimit(0))
      .Times(0);

  EXPECT_CALL(*client_, limit(_, "foo",
                              testing::ContainerEq(std::vector<Envoy::RateLimit::Descriptor>{
                                  {{{"descriptor_key", "descriptor_value"}}}}),
                              _))
      .WillOnce(WithArgs<0>(Invoke([&](Envoy::RateLimit::RequestCallbacks& callbacks) -> void {
        callbacks.complete(Envoy::RateLimit::LimitStatus::OK);
      })));

  EXPECT_CALL(filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers_));

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("ratelimit.ok").value());
}

TEST_F(HttpRateLimitFilterTest, ConfigValueTest) {
  std::string stage_filter_config = R"EOF(
  {
    "domain": "foo",
    "stage": 5,
    "request_type" : "internal"
  }
  )EOF";

  SetUpTest(stage_filter_config);

  EXPECT_EQ(5UL, config_->stage());
  EXPECT_EQ("foo", config_->domain());
  EXPECT_EQ(FilterRequestType::Internal, config_->requestType());
}

TEST_F(HttpRateLimitFilterTest, DefaultConfigValueTest) {
  std::string stage_filter_config = R"EOF(
  {
    "domain": "foo"
  }
  )EOF";

  SetUpTest(stage_filter_config);

  EXPECT_EQ(0UL, config_->stage());
  EXPECT_EQ("foo", config_->domain());
  EXPECT_EQ(FilterRequestType::Both, config_->requestType());
}

} // namespace RateLimit
} // namespace Http
} // namespace Envoy
