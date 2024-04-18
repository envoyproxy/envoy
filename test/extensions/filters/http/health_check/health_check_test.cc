#include <chrono>
#include <memory>

#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/http/health_check/health_check.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {
namespace {

class HealthCheckFilterTest : public testing::Test {
public:
  HealthCheckFilterTest(bool pass_through, bool caching)
      : request_headers_{{":path", "/healthcheck"}}, request_headers_no_hc_{{":path", "/foo"}} {

    if (caching) {
      cache_timer_ = new Event::MockTimer(&dispatcher_);
      EXPECT_CALL(*cache_timer_, enableTimer(_, _));
      cache_manager_ =
          std::make_shared<HealthCheckCacheManager>(dispatcher_, std::chrono::milliseconds(1));
    }

    prepareFilter(pass_through);
  }

  void prepareFilter(
      bool pass_through,
      ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages = nullptr) {
    header_data_ = std::make_shared<std::vector<Http::HeaderUtility::HeaderDataPtr>>();
    envoy::config::route::v3::HeaderMatcher matcher;
    matcher.set_name(":path");
    matcher.mutable_string_match()->set_exact("/healthcheck");
    header_data_->emplace_back(
        std::make_unique<Http::HeaderUtility::HeaderData>(matcher, context_));
    filter_ = std::make_unique<HealthCheckFilter>(context_, pass_through, cache_manager_,
                                                  header_data_, cluster_min_healthy_percentages);
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Event::MockTimer* cache_timer_{};
  Event::MockDispatcher dispatcher_;
  HealthCheckCacheManagerSharedPtr cache_manager_;
  std::unique_ptr<HealthCheckFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestHeaderMapImpl request_headers_no_hc_;
  HeaderDataVectorSharedPtr header_data_;

  class MockHealthCheckCluster : public NiceMock<Upstream::MockThreadLocalCluster> {
  public:
    MockHealthCheckCluster(uint64_t membership_total, uint64_t membership_healthy,
                           uint64_t membership_degraded = 0) {
      info()->endpointStats().membership_total_.set(membership_total);
      info()->endpointStats().membership_healthy_.set(membership_healthy);
      info()->endpointStats().membership_degraded_.set(membership_degraded);
    }
  };
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
  EXPECT_CALL(context_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
}

TEST_F(HealthCheckFilterNoPassThroughTest, NotHcRequest) {
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));

  Http::TestResponseHeaderMapImpl service_response{{":status", "200"}};
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_response, false));
  Buffer::OwnedImpl body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(body, false));
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_EQ("true", service_response.getEnvoyImmediateHealthCheckFailValue());
}

TEST_F(HealthCheckFilterNoPassThroughTest, ComputedHealth) {
  // Test non-pass-through health checks without upstream cluster minimum health specified.
  prepareFilter(false);
  {
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "200"}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_ok", callbacks_.details());
  }
  {
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_failed", callbacks_.details());
  }

  // Test non-pass-through health checks with upstream cluster minimum health specified.
  prepareFilter(false, ClusterMinHealthyPercentagesConstSharedPtr(
                           new ClusterMinHealthyPercentages{{"www1", 50.0}, {"www2", 75.0}}));
  {
    // This should pass, because each upstream cluster has at least the
    // minimum percentage of healthy servers.
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "200"}};
    MockHealthCheckCluster cluster_www1(100, 50);
    MockHealthCheckCluster cluster_www2(1000, 800);
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager());
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www1")))
        .WillRepeatedly(Return(&cluster_www1));
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www2")))
        .WillRepeatedly(Return(&cluster_www2));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_ok_cluster_healthy", callbacks_.details());
  }
  {
    // This should fail, because one upstream cluster has too few healthy servers.
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};
    MockHealthCheckCluster cluster_www1(100, 49);
    MockHealthCheckCluster cluster_www2(1000, 800);
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager());
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www1")))
        .WillRepeatedly(Return(&cluster_www1));
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www2")))
        .WillRepeatedly(Return(&cluster_www2));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_failed_cluster_unhealthy", callbacks_.details());
  }
  {
    // This should fail, because one upstream cluster has no servers at all.
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};
    MockHealthCheckCluster cluster_www1(0, 0);
    MockHealthCheckCluster cluster_www2(1000, 800);
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager());
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www1")))
        .WillRepeatedly(Return(&cluster_www1));
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www2")))
        .WillRepeatedly(Return(&cluster_www2));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_failed_cluster_empty", callbacks_.details());
  }
  // Test the cases where an upstream cluster is empty, or has no healthy servers, but
  // the minimum required percent healthy is zero. The health check should return a 200.
  prepareFilter(false, ClusterMinHealthyPercentagesConstSharedPtr(
                           new ClusterMinHealthyPercentages{{"www1", 0.0}, {"www2", 0.0}}));
  {
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "200"}};
    MockHealthCheckCluster cluster_www1(0, 0);
    MockHealthCheckCluster cluster_www2(1000, 0);
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager());
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www1")))
        .WillRepeatedly(Return(&cluster_www1));
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www2")))
        .WillRepeatedly(Return(&cluster_www2));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_ok_cluster_healthy", callbacks_.details());
  }
  {
    // This should succeed, because each cluster has degraded + healthy hosts greater than the
    // threshold.
    Http::TestResponseHeaderMapImpl health_check_response{{":status", "200"}};
    MockHealthCheckCluster cluster_www1(100, 40, 20);
    MockHealthCheckCluster cluster_www2(1000, 0, 800);
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager());
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www1")))
        .WillRepeatedly(Return(&cluster_www1));
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq("www2")))
        .WillRepeatedly(Return(&cluster_www2));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
    EXPECT_EQ("health_check_ok_cluster_healthy", callbacks_.details());
  }
}

TEST_F(HealthCheckFilterNoPassThroughTest, HealthCheckFailedCallbackCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_EQ("cluster_name", headers.getEnvoyUpstreamHealthCheckedClusterValue());
        EXPECT_NE(nullptr, headers.EnvoyImmediateHealthCheckFail());
      }));

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
}

TEST_F(HealthCheckFilterPassThroughTest, Ok) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(2).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Http::TestResponseHeaderMapImpl service_hc_response{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_hc_response, true));
  EXPECT_EQ("cluster_name", service_hc_response.getEnvoyUpstreamHealthCheckedClusterValue());
}

TEST_F(HealthCheckFilterPassThroughTest, OkWithContinue) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(2).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Goodness only knows why there would be a 100-Continue response in health
  // checks but we can still verify Envoy handles it.
  Http::TestResponseHeaderMapImpl continue_response{{":status", "100"}};
  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(continue_response));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  Http::TestResponseHeaderMapImpl service_hc_response{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_hc_response, true));
  EXPECT_EQ("cluster_name", service_hc_response.getEnvoyUpstreamHealthCheckedClusterValue());
}

TEST_F(HealthCheckFilterPassThroughTest, Failed) {
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(HealthCheckFilterPassThroughTest, NotHcRequest) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedServiceUnavailableCallbackCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  cache_manager_->setCachedResponse(Http::Code::ServiceUnavailable, false);

  Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_EQ("cluster_name", headers.getEnvoyUpstreamHealthCheckedClusterValue());
      }));

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedOkCallbackNotCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).WillRepeatedly(Return(false));
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true));
  EXPECT_CALL(callbacks_.active_span_, setSampled(false));
  cache_manager_->setCachedResponse(Http::Code::OK, false);

  Http::TestResponseHeaderMapImpl health_check_response{{":status", "200"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_EQ("cluster_name", headers.getEnvoyUpstreamHealthCheckedClusterValue());
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ("health_check_cached", callbacks_.details());
}

TEST_F(HealthCheckFilterCachingTest, All) {
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true)).Times(3);
  EXPECT_CALL(callbacks_.active_span_, setSampled(false)).Times(3);

  // Verify that the first request goes through.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  Http::TestResponseHeaderMapImpl service_response_headers{{":status", "503"}};
  Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(service_response_headers, true));

  // Verify that the next request uses the cached value without setting the degraded header.
  prepareFilter(true);
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FailedLocalHealthCheck));
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_EQ("cluster_name", headers.getEnvoyUpstreamHealthCheckedClusterValue());
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));

  // Fire the timer, this should result in the next request going through.
  EXPECT_CALL(*cache_timer_, enableTimer(_, _));
  cache_timer_->invokeCallback();
  prepareFilter(true);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, DegradedHeader) {
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(true)).Times(3);
  EXPECT_CALL(callbacks_.active_span_, setSampled(false)).Times(3);

  // Verify that the first request goes through.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  Http::TestResponseHeaderMapImpl service_response_headers{{":status", "503"},
                                                           {"x-envoy-degraded", "true"}};
  Http::TestResponseHeaderMapImpl health_check_response{{":status", "503"},
                                                        {"x-envoy-degraded", ""}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(service_response_headers, true));

  // Verify that the next request uses the cached value and that the x-envoy-degraded header is set.
  prepareFilter(true);
  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FailedLocalHealthCheck));
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::ResponseHeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_EQ("cluster_name", headers.getEnvoyUpstreamHealthCheckedClusterValue());
      }));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));

  // Fire the timer, this should result in the next request going through.
  EXPECT_CALL(*cache_timer_, enableTimer(_, _));
  cache_timer_->invokeCallback();
  prepareFilter(true);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, NotHcRequest) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.stream_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

} // namespace
} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
