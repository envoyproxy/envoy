#include <chrono>
#include <memory>

#include "common/buffer/buffer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "server/http/health_check.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {

class HealthCheckFilterTest : public testing::Test {
public:
  HealthCheckFilterTest(bool pass_through, bool caching)
      : request_headers_{{":path", "/healthcheck"}}, request_headers_no_hc_{{":path", "/foo"}} {

    if (caching) {
      cache_timer_ = new Event::MockTimer(&dispatcher_);
      EXPECT_CALL(*cache_timer_, enableTimer(_));
      cache_manager_.reset(new HealthCheckCacheManager(dispatcher_, std::chrono::milliseconds(1)));
    }

    prepareFilter(pass_through);
  }

  void
  prepareFilter(bool pass_through,
                ClusterMinHealthyPercentagesSharedPtr cluster_min_healthy_percentages = nullptr) {
    filter_.reset(new HealthCheckFilter(context_, pass_through, cache_manager_, "/healthcheck",
                                        cluster_min_healthy_percentages));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Event::MockTimer* cache_timer_{};
  Event::MockDispatcher dispatcher_;
  HealthCheckCacheManagerSharedPtr cache_manager_;
  std::unique_ptr<HealthCheckFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
  Http::TestHeaderMapImpl request_headers_no_hc_;

  class MockHealthCheckCluster : public Upstream::MockCluster {
  public:
    MockHealthCheckCluster(uint64_t membership_total, uint64_t membership_healthy)
        : info_(new ClusterInfo(membership_total, membership_healthy)) {}

    Upstream::ClusterInfoConstSharedPtr info() const override { return info_; }

    class ClusterInfo : public Upstream::MockClusterInfo {
    public:
      ClusterInfo(uint64_t membership_total, uint64_t membership_healthy)
          : stats_(Upstream::ClusterInfoImpl::generateStats(stats_store_)) {
        stats_.membership_total_.set(membership_total);
        stats_.membership_healthy_.set(membership_healthy);
      }

      Upstream::ClusterStats& stats() const override { return stats_; }

    private:
      mutable Upstream::ClusterStats stats_;
    };

    Upstream::ClusterInfoConstSharedPtr info_;
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
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(HealthCheckFilterNoPassThroughTest, NotHcRequest) {
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));

  Http::TestHeaderMapImpl service_response{{":status", "200"}};
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_response, true));
  EXPECT_STREQ("true", service_response.EnvoyImmediateHealthCheckFail()->value().c_str());
}

TEST_F(HealthCheckFilterNoPassThroughTest, ComputedHealth) {
  // Test health non-pass-through health checks without upstream cluster
  // minimum health specified.
  prepareFilter(false);
  {
    Http::TestHeaderMapImpl health_check_response{{":status", "200"}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
        .Times(1);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
  }
  {
    Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
        .Times(1);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
  }

  // Test health non-pass-through health checks with upstream cluster
  // minimum health specified.
  prepareFilter(false, ClusterMinHealthyPercentagesSharedPtr(
                           new ClusterMinHealthyPercentages{{"www1", 50.0}, {"www2", 75.0}}));
  {
    // This should pass, because each upstream cluster has at least the
    // minimum percentage of healthy servers.
    Http::TestHeaderMapImpl health_check_response{{":status", "200"}};
    MockHealthCheckCluster cluster_www1(100, 50);
    MockHealthCheckCluster cluster_www2(1000, 800);
    Upstream::ClusterManager::ClusterInfoMap cluster_info_map{
        {"www1", std::reference_wrapper<const Upstream::Cluster>(cluster_www1)},
        {"www2", std::reference_wrapper<const Upstream::Cluster>(cluster_www2)}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager()).Times(1);
    EXPECT_CALL(context_.cluster_manager_, clusters()).WillOnce(Return(cluster_info_map));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
        .Times(1);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
  }
  {
    // This should fail, because one upstream cluster has too few healthy servers.
    Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
    MockHealthCheckCluster cluster_www1(100, 49);
    MockHealthCheckCluster cluster_www2(1000, 800);
    Upstream::ClusterManager::ClusterInfoMap cluster_info_map{
        {"www1", std::reference_wrapper<const Upstream::Cluster>(cluster_www1)},
        {"www2", std::reference_wrapper<const Upstream::Cluster>(cluster_www2)}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager()).Times(1);
    EXPECT_CALL(context_.cluster_manager_, clusters()).WillOnce(Return(cluster_info_map));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
        .Times(1);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
  }
  {
    // This should fail, because one upstream cluster has no servers at all.
    Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
    MockHealthCheckCluster cluster_www1(0, 0);
    MockHealthCheckCluster cluster_www2(1000, 800);
    Upstream::ClusterManager::ClusterInfoMap cluster_info_map{
        {"www1", std::reference_wrapper<const Upstream::Cluster>(cluster_www1)},
        {"www2", std::reference_wrapper<const Upstream::Cluster>(cluster_www2)}};
    EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
    EXPECT_CALL(context_, clusterManager()).Times(1);
    EXPECT_CALL(context_.cluster_manager_, clusters()).WillOnce(Return(cluster_info_map));
    EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
        .Times(1);
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(request_headers_, true));
  }
}

TEST_F(HealthCheckFilterNoPassThroughTest, HealthCheckFailedCallbackCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  Http::TestHeaderMapImpl health_check_response{{":status", "503"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&health_check_response), true))
      .Times(1)
      .WillRepeatedly(Invoke([&](Http::HeaderMap& headers, bool end_stream) {
        filter_->encodeHeaders(headers, end_stream);
        EXPECT_STREQ("cluster_name", headers.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
        EXPECT_EQ(nullptr, headers.EnvoyImmediateHealthCheckFail());
      }));

  EXPECT_CALL(callbacks_.request_info_,
              setResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data, false));

  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_headers_));
}

TEST_F(HealthCheckFilterPassThroughTest, Ok) {
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(false));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  Http::TestHeaderMapImpl service_hc_respnose{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(service_hc_respnose, true));
  EXPECT_STREQ("cluster_name",
               service_hc_respnose.EnvoyUpstreamHealthCheckedCluster()->value().c_str());
}

TEST_F(HealthCheckFilterPassThroughTest, Failed) {
  EXPECT_CALL(context_, healthCheckFailed()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_.request_info_, healthCheck(true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(HealthCheckFilterPassThroughTest, NotHcRequest) {
  EXPECT_CALL(context_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedServiceUnavailableCallbackCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).WillRepeatedly(Return(false));
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
              setResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));
}

TEST_F(HealthCheckFilterCachingTest, CachedOkCallbackNotCalled) {
  EXPECT_CALL(context_, healthCheckFailed()).WillRepeatedly(Return(false));
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
              setResponseFlag(RequestInfo::ResponseFlag::FailedLocalHealthCheck));
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
  EXPECT_CALL(context_, healthCheckFailed()).Times(0);
  EXPECT_CALL(callbacks_.request_info_, healthCheck(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(request_headers_no_hc_, true));
}

TEST(HealthCheckFilterConfig, failsWhenNotPassThroughButTimeoutSetJson) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(
      "{\"pass_through_mode\":false, \"cache_time_ms\":234, \"endpoint\":\"foo\"}");
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(healthCheckFilterConfig.createFilterFactory(*config, "dummy_stats_prefix", context),
               EnvoyException);
}

TEST(HealthCheckFilterConfig, notFailingWhenNotPassThroughAndTimeoutNotSetJson) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  Json::ObjectSharedPtr config =
      Json::Factory::loadFromString("{\"pass_through_mode\":false, \"endpoint\":\"foo\"}");
  NiceMock<Server::Configuration::MockFactoryContext> context;

  healthCheckFilterConfig.createFilterFactory(*config, "dummy_stats_prefix", context);
}

TEST(HealthCheckFilterConfig, failsWhenNotPassThroughButTimeoutSetProto) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::api::v2::filter::http::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  config.mutable_cache_time()->set_seconds(10);

  EXPECT_THROW(
      healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context),
      EnvoyException);
}

TEST(HealthCheckFilterConfig, notFailingWhenNotPassThroughAndTimeoutNotSetProto) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  envoy::api::v2::filter::http::HealthCheck config{};
  NiceMock<Server::Configuration::MockFactoryContext> context;

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}

TEST(HealthCheckFilterConfig, HealthCheckFilterWithEmptyProto) {
  Server::Configuration::HealthCheckFilterConfig healthCheckFilterConfig;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  envoy::api::v2::filter::http::HealthCheck config =
      *dynamic_cast<envoy::api::v2::filter::http::HealthCheck*>(
          healthCheckFilterConfig.createEmptyConfigProto().get());

  config.mutable_pass_through_mode()->set_value(false);
  config.set_endpoint("foo");
  healthCheckFilterConfig.createFilterFactoryFromProto(config, "dummy_stats_prefix", context);
}
} // namespace Envoy
