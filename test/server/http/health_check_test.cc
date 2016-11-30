#include "server/http/health_check.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

class HealthCheckFilterTest : public testing::Test {
public:
  HealthCheckFilterTest(bool pass_through, bool caching)
      : request_headers_{{":path", "/healthcheck"}}, request_headers_no_hc_{{":path", "/foo"}} {

    ON_CALL(server_.options_, serviceClusterName()).WillByDefault(ReturnRef(cluster_name_));
    if (caching) {
      cache_timer_ = new Event::MockTimer(&dispatcher_);
      EXPECT_CALL(*cache_timer_, enableTimer(_));
      cache_manager_.reset(new HealthCheckCacheManager(dispatcher_, std::chrono::milliseconds(1)));
    }

    prepareFilter(pass_through);
  }

  void prepareFilter(bool pass_through) {
    filter_.reset(new HealthCheckFilter(server_, pass_through, cache_manager_, "/healthcheck"));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<Server::MockInstance> server_;
  Event::MockTimer* cache_timer_{};
  Event::MockDispatcher dispatcher_;
  HealthCheckCacheManagerPtr cache_manager_;
  std::unique_ptr<HealthCheckFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
  Http::TestHeaderMapImpl request_headers_no_hc_;
  std::string cluster_name_{"cluster_name"};
};

class HealthCheckFilterNoPassThroughTest : public HealthCheckFilterTest {
public:
  HealthCheckFilterNoPassThroughTest() : HealthCheckFilterTest(false, false) {}
};

class HealthCheckFilterPassThroughTest : public HealthCheckFilterTest {
public:
  HealthCheckFilterPassThroughTest() : HealthCheckFilterTest(true, false) {}
};

class HealthCheckFilterCachingTest : public HealthCheckFilterTest {
public:
  HealthCheckFilterCachingTest() : HealthCheckFilterTest(true, true) {}
};

TEST_F(HealthCheckFilterNoPassThroughTest, OkOrFailed) {
  EXPECT_CALL(server_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(HealthCheckFilterNoPassThroughTest, NotHcRequest) {
  EXPECT_CALL(server_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST_F(HealthCheckFilterNoPassThroughTest, HealthCheckFailedCallbackCalled) {
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::HeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_STREQ("cluster_name", headers.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
      }));

  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));
}

TEST_F(HealthCheckFilterPassThroughTest, Ok) {
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl service_hc_respnose{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_hc_respnose, true));
  EXPECT_STREQ("cluster_name",
               service_hc_respnose.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
}

TEST_F(HealthCheckFilterPassThroughTest, Failed) {
  EXPECT_CALL(server_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(HealthCheckFilterPassThroughTest, NotHcRequest) {
  EXPECT_CALL(server_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedServiceUnavailableCallbackCalled) {
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  cache_manager_->setCachedResponseCode(Http::Code::ServiceUnavailable);

  Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::HeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_STREQ("cluster_name", headers.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
      }));

  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedOkCallbackNotCalled) {
  EXPECT_CALL(server_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  cache_manager_->setCachedResponseCode(Http::Code::OK);

  Http::TestHeaderMapImpl health_check_response{{":status", "200"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::HeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_STREQ("cluster_name", headers.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, All) {
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true)).Times(3);

  // Verify that the first request goes through.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  Http::TestHeaderMapImpl service_response_headers{{":status", "503"}};
  Http::TestHeaderMapImpl health_check_response{{":status", "503"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(service_response_headers, true));

  // Verify that the next request uses the cached value.
  prepareFilter(true);
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::FailedLocalHealthCheck));
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::HeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_STREQ("cluster_name", headers.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));

  // Fire the timer, this should result in the next request going through.
  EXPECT_CALL(*cache_timer_, enableTimer(_));
  cache_timer_->callback_();
  prepareFilter(true);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, NotHcRequest) {
  EXPECT_CALL(server_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST(HealthCheckFilterConfig, failsWhenNotPassThroughButTimeoutSet) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectPtr config = Json::Factory::LoadFromString(
      "{\"pass_through_mode\":false, \"cache_time_ms\":234, \"endpoint\":\"foo\"}");
  NiceMock<Server::MockInstance> serverMock;

  EXPECT_THROW(healthCheckFilterConfig.tryCreateFilterFactory(
                   Server::Configuration::HttpFilterType::Both, "health_check", *config,
                   "dummy_stats_prefix", serverMock),
               EnvoyException);
}

TEST(HealthCheckFilterConfig, notFailingWhenNotPassThroughAndTimeoutNotSet) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectPtr config =
      Json::Factory::LoadFromString("{\"pass_through_mode\":false, \"endpoint\":\"foo\"}");
  NiceMock<Server::MockInstance> serverMock;

  healthCheckFilterConfig.tryCreateFilterFactory(Server::Configuration::HttpFilterType::Both,
                                                 "health_check", *config, "dummy_stats_prefix",
                                                 serverMock);
}
