#include "common/router/router.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Router {

class TestFilter : public Filter {
public:
  using Filter::Filter;

  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::HeaderMap&, const Upstream::Cluster&,
                                 Runtime::Loader&, Runtime::RandomGenerator&,
                                 Event::Dispatcher&) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    return RetryStatePtr{retry_state_};
  }

  MockRetryState* retry_state_{};
};

class RouterTest : public testing::Test {
public:
  RouterTest() : router_("test.", stats_store_, cm_, runtime_, random_) {
    router_.setDecoderFilterCallbacks(callbacks_);
    ON_CALL(*cm_.conn_pool_.host_, url()).WillByDefault(ReturnRef(host_url_));
  }

  void expectTimerCreate() {
    response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Http::ConnectionPool::MockCancellable cancellable_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  TestFilter router_;
  Event::MockTimer* response_timeout_{};
  std::string host_url_{"tcp://10.0.0.5:9211"};
};

TEST_F(RouterTest, RouteNotFound) {
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::NoRouteFound));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(nullptr));

  router_.decodeHeaders(headers, true);
}

TEST_F(RouterTest, PoolFailure) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             callbacks.onPoolFailure(
                                 Http::ConnectionPool::PoolFailureReason::ConnectionFailure,
                                 cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  Http::HeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "57"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::UpstreamConnectionFailure));
  EXPECT_CALL(callbacks_.request_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionPtr host)
                           -> void { EXPECT_EQ(host_url_, host->url()); }));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

TEST_F(RouterTest, CancelBeforeBoundToPool) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _)).WillOnce(Return(&cancellable_));
  expectTimerCreate();

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel());
  callbacks_.reset_callback_();
}

TEST_F(RouterTest, NoHost) {
  EXPECT_CALL(cm_, httpConnPoolForCluster(_)).WillOnce(Return(nullptr));

  Http::HeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "19"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::NoHealthyUpstream));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

TEST_F(RouterTest, MaintenanceMode) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.maintenance_mode.fake_cluster", 0))
      .WillOnce(Return(true));

  Http::HeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "16"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::UpstreamOverflow));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

TEST_F(RouterTest, ResetDuringEncodeHeaders) {
  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(encoder, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const Http::HeaderMap&, bool) -> void {
        encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
      }));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

TEST_F(RouterTest, UpstreamTimeout) {
  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  EXPECT_CALL(callbacks_.request_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionPtr host)
                           -> void { EXPECT_EQ(host_url_, host->url()); }));

  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);

  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::HeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  response_timeout_->callback_();

  EXPECT_EQ(1U,
            cm_.cluster_.stats_store_.counter("cluster.fake_cluster.upstream_rq_timeout").value());
}

TEST_F(RouterTest, RetryRequestNotComplete) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::UpstreamRemoteReset));
  EXPECT_CALL(callbacks_.request_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionPtr host)
                           -> void { EXPECT_EQ(host_url_, host->url()); }));

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  router_.retry_state_->expectRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
}

TEST_F(RouterTest, RetryNoneHealthy) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  expectTimerCreate();
  EXPECT_CALL(callbacks_.request_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionPtr host)
                           -> void { EXPECT_EQ(host_url_, host->url()); }));

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  router_.retry_state_->expectRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::LocalReset);

  EXPECT_CALL(cm_, httpConnPoolForCluster(_)).WillOnce(Return(nullptr));
  Http::HeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "19"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::NoHealthyUpstream));
  router_.retry_state_->callback_();
}

TEST_F(RouterTest, RetryUpstreamReset) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  router_.retry_state_->expectRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // We expect this reset to kick off a new request.
  NiceMock<Http::MockStreamEncoder> encoder2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder2, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  router_.retry_state_->callback_();

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).WillOnce(Return(false));
  Http::HeaderMapPtr response_headers(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

TEST_F(RouterTest, RetryUpstreamResetResponseStarted) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // Since the response is already started we don't retry.
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  Http::HeaderMapPtr response_headers(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
}

TEST_F(RouterTest, RetryUpstream5xx) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // 5xx response.
  router_.retry_state_->expectRetry();
  Http::HeaderMapPtr response_headers1(new Http::HeaderMapImpl{{":status", "503"}});
  response_decoder->decodeHeaders(std::move(response_headers1), true);

  // We expect the 5xx response to kick off a new request.
  EXPECT_CALL(encoder1.stream_, resetStream(_)).Times(0);
  NiceMock<Http::MockStreamEncoder> encoder2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder2, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  router_.retry_state_->callback_();

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).WillOnce(Return(false));
  Http::HeaderMapPtr response_headers2(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers2), true);
}

TEST_F(RouterTest, RetryTimeoutDuringRetryDelay) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // 5xx response.
  router_.retry_state_->expectRetry();
  Http::HeaderMapPtr response_headers1(new Http::HeaderMapImpl{{":status", "503"}});
  response_decoder->decodeHeaders(std::move(response_headers1), true);

  // Fire timeout.
  EXPECT_CALL(callbacks_.request_info_,
              onFailedResponse(Http::AccessLog::FailureReason::UpstreamRequestTimeout));
  Http::HeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  response_timeout_->callback_();
}

TEST_F(RouterTest, RetryUpstream5xxNotComplete) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  Buffer::OwnedImpl body_data("hello");
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, router_.decodeData(body_data, true));

  // 5xx response.
  router_.retry_state_->expectRetry();
  Http::HeaderMapPtr response_headers1(new Http::HeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(encoder1.stream_, resetStream(Http::StreamResetReason::LocalReset));
  response_decoder->decodeHeaders(std::move(response_headers1), false);

  // We expect the 5xx response to kick off a new request.
  NiceMock<Http::MockStreamEncoder> encoder2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder2, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  ON_CALL(callbacks_, decodingBuffer()).WillByDefault(Return(&body_data));
  EXPECT_CALL(encoder2, encodeHeaders(_, false));
  EXPECT_CALL(encoder2, encodeData(_, true));
  router_.retry_state_->callback_();

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).WillOnce(Return(false));
  Http::HeaderMapPtr response_headers2(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers2), true);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.retry.upstream_rq_503").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_200").value());
}

TEST_F(RouterTest, AltStatName) {
  // Also test no upstream timeout here.
  NiceMock<MockRouteEntry> route_entry;
  EXPECT_CALL(callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(&route_entry));
  EXPECT_CALL(route_entry, timeout()).WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  Http::HeaderMapImpl headers{{"x-envoy-upstream-alt-stat-name", "alt_stat"},
                              {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  Http::HeaderMapPtr response_headers(
      new Http::HeaderMapImpl{{":status", "200"},
                              {"x-envoy-upstream-canary", "true"},
                              {"x-envoy-virtual-cluster", "hello"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);

  EXPECT_EQ(1U,
            stats_store_.counter("vhost.fake_vhost.vcluster.fake_virtual_cluster.upstream_rq_200")
                .value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.canary.upstream_rq_200").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.alt_stat.upstream_rq_200").value());
}

TEST_F(RouterTest, Redirect) {
  MockRedirectEntry redirect;
  EXPECT_CALL(redirect, newPath(_)).WillOnce(Return("hello"));
  EXPECT_CALL(callbacks_.route_table_, redirectRequest(_)).WillOnce(Return(&redirect));

  Http::HeaderMapImpl response_headers{{":status", "301"}, {"location", "hello"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(response_headers), true));
  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

TEST(RouterFilterUtilityTest, All) {
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers;
    EXPECT_EQ(std::chrono::milliseconds(10), FilterUtility::finalTimeout(route, headers));
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"}};
    EXPECT_EQ(std::chrono::milliseconds(15), FilterUtility::finalTimeout(route, headers));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("15", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "bad"}};
    EXPECT_EQ(std::chrono::milliseconds(10), FilterUtility::finalTimeout(route, headers));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("10", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
}

} // Router
