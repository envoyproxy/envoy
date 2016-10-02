#include "common/router/router.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::AtLeast;
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
                                 Runtime::Loader&, Runtime::RandomGenerator&, Event::Dispatcher&,
                                 Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    return RetryStatePtr{retry_state_};
  }

  MockRetryState* retry_state_{};
};

class RouterTest : public testing::Test {
public:
  RouterTest()
      : shadow_writer_(new MockShadowWriter()),
        config_("test.", "from_az", stats_store_, cm_, runtime_, random_,
                ShadowWriterPtr{shadow_writer_}, true),
        router_(config_) {
    router_.setDecoderFilterCallbacks(callbacks_);
    ON_CALL(*cm_.conn_pool_.host_, url()).WillByDefault(ReturnRef(host_url_));
    ON_CALL(*cm_.conn_pool_.host_, zone()).WillByDefault(ReturnRef(upstream_zone_));
  }

  void expectResponseTimerCreate() {
    response_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  void expectPerTryTimerCreate() {
    per_try_timeout_ = new Event::MockTimer(&callbacks_.dispatcher_);
    EXPECT_CALL(*per_try_timeout_, enableTimer(_));
    EXPECT_CALL(*per_try_timeout_, disableTimer());
  }

  std::string upstream_zone_{"to_az"};
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Http::ConnectionPool::MockCancellable cancellable_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  MockShadowWriter* shadow_writer_;
  FilterConfig config_;
  TestFilter router_;
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};
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

TEST_F(RouterTest, PoolFailureWithPriority) {
  NiceMock<MockRouteEntry> route_entry;
  EXPECT_CALL(callbacks_.route_table_, routeForRequest(_)).WillOnce(Return(&route_entry));
  route_entry.virtual_cluster_.priority_ = Upstream::ResourcePriority::High;
  EXPECT_CALL(cm_, httpConnPoolForCluster(_, Upstream::ResourcePriority::High));

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
  expectResponseTimerCreate();

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel());
  callbacks_.reset_callback_();
}

TEST_F(RouterTest, NoHost) {
  EXPECT_CALL(cm_, httpConnPoolForCluster(_, _)).WillOnce(Return(nullptr));

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

  expectResponseTimerCreate();

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
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).Times(0);
  response_timeout_->callback_();

  EXPECT_EQ(1U,
            cm_.cluster_.stats_store_.counter("cluster.fake_cluster.upstream_rq_timeout").value());
}

TEST_F(RouterTest, UpstreamPerTryTimeout) {
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

  expectResponseTimerCreate();
  expectPerTryTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-internal", "true"},
                              {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
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
  per_try_timeout_->callback_();

  EXPECT_EQ(1U,
            cm_.cluster_.stats_store_.counter("cluster.fake_cluster.upstream_rq_per_try_timeout")
                .value());
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

  expectResponseTimerCreate();
  EXPECT_CALL(callbacks_.request_info_, onUpstreamHostSelected(_))
      .WillOnce(Invoke([&](const Upstream::HostDescriptionPtr host)
                           -> void { EXPECT_EQ(host_url_, host->url()); }));

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  router_.retry_state_->expectRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::LocalReset);

  EXPECT_CALL(cm_, httpConnPoolForCluster(_, _)).WillOnce(Return(nullptr));
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
  expectResponseTimerCreate();

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

TEST_F(RouterTest, RetryUpstreamPerTryTimeout) {
  NiceMock<Http::MockStreamEncoder> encoder1;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder1, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectResponseTimerCreate();
  expectPerTryTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                              {"x-envoy-internal", "true"},
                              {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);

  router_.retry_state_->expectRetry();
  per_try_timeout_->callback_();

  // We expect this reset to kick off a new request.
  NiceMock<Http::MockStreamEncoder> encoder2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder2, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectPerTryTimerCreate();
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
  expectResponseTimerCreate();

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
  expectResponseTimerCreate();

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
  expectResponseTimerCreate();

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
  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putHttpResponseCode(504));
  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putResponseTime(_)).Times(0);
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
  expectResponseTimerCreate();

  Http::HeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  Buffer::OwnedImpl body_data("hello");
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, router_.decodeData(body_data, false));

  Http::HeaderMapImpl trailers{{"some", "trailer"}};
  router_.decodeTrailers(trailers);

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
  EXPECT_CALL(encoder2, encodeData(_, false));
  EXPECT_CALL(encoder2, encodeTrailers(_));
  router_.retry_state_->callback_();

  // Normal response.
  EXPECT_CALL(*router_.retry_state_, shouldRetry(_, _, _)).WillOnce(Return(false));
  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putHttpResponseCode(200));
  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putResponseTime(_));
  Http::HeaderMapPtr response_headers2(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers2), true);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.retry.upstream_rq_503").value());
  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.upstream_rq_200").value());
  EXPECT_EQ(
      1U, stats_store_.counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_200").value());
  EXPECT_EQ(
      1U, stats_store_.counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_2xx").value());
}

TEST_F(RouterTest, Shadow) {
  callbacks_.route_table_.route_entry_.shadow_policy_.cluster_ = "foo";
  callbacks_.route_table_.route_entry_.shadow_policy_.runtime_key_ = "bar";
  ON_CALL(callbacks_, streamId()).WillByDefault(Return(43));

  NiceMock<Http::MockStreamEncoder> encoder;
  Http::StreamDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](Http::StreamDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks)
                           -> Http::ConnectionPool::Cancellable* {
                             response_decoder = &decoder;
                             callbacks.onPoolReady(encoder, cm_.conn_pool_.host_);
                             return nullptr;
                           }));
  expectResponseTimerCreate();

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("bar", 0, 43, 10000)).WillOnce(Return(true));

  Http::HeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);

  Buffer::OwnedImpl body_data("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, router_.decodeData(body_data, false));

  Http::HeaderMapImpl trailers{{"some", "trailer"}};
  EXPECT_CALL(callbacks_, decodingBuffer()).Times(AtLeast(1)).WillRepeatedly(Return(&body_data));
  EXPECT_CALL(*shadow_writer_, shadow_("foo", _, std::chrono::milliseconds(10)))
      .WillOnce(Invoke([](const std::string&, Http::MessagePtr& request, std::chrono::milliseconds)
                           -> void {
                             EXPECT_NE(nullptr, request->body());
                             EXPECT_NE(nullptr, request->trailers());
                           }));
  router_.decodeTrailers(trailers);

  Http::HeaderMapPtr response_headers(new Http::HeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);
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

  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putHttpResponseCode(200));
  EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putResponseTime(_));

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
  EXPECT_EQ(1U,
            stats_store_.counter("cluster.fake_cluster.alt_stat.zone.from_az.to_az.upstream_rq_200")
                .value());
  EXPECT_EQ(1U,
            stats_store_.counter("cluster.fake_cluster.alt_stat.zone.from_az.to_az.upstream_rq_200")
                .value());
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

TEST(RouterFilterUtilityTest, finalTimeout) {
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers;
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(route, headers);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(route, headers);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("15", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "bad"}};
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(route, headers);
    EXPECT_EQ(std::chrono::milliseconds(10), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_EQ("10", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                {"x-envoy-upstream-rq-per-try-timeout-ms", "15"}};
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(route, headers);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(0), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("15", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
  {
    MockRouteEntry route;
    EXPECT_CALL(route, timeout()).WillOnce(Return(std::chrono::milliseconds(10)));
    Http::HeaderMapImpl headers{{"x-envoy-upstream-rq-timeout-ms", "15"},
                                {"x-envoy-upstream-rq-per-try-timeout-ms", "5"}};
    FilterUtility::TimeoutData timeout = FilterUtility::finalTimeout(route, headers);
    EXPECT_EQ(std::chrono::milliseconds(15), timeout.global_timeout_);
    EXPECT_EQ(std::chrono::milliseconds(5), timeout.per_try_timeout_);
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-timeout-ms"));
    EXPECT_FALSE(headers.has("x-envoy-upstream-rq-per-try-timeout-ms"));
    EXPECT_EQ("5", headers.get("x-envoy-expected-rq-timeout-ms"));
  }
}

TEST(RouterFilterUtilityTest, shouldShadow) {
  {
    TestShadowPolicy policy;
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled(_, _, _, _)).Times(0);
    EXPECT_FALSE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy;
    policy.cluster_ = "cluster";
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled(_, _, _, _)).Times(0);
    EXPECT_TRUE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy;
    policy.cluster_ = "cluster";
    policy.runtime_key_ = "foo";
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("foo", 0, 5, 10000)).WillOnce(Return(false));
    EXPECT_FALSE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
  {
    TestShadowPolicy policy;
    policy.cluster_ = "cluster";
    policy.runtime_key_ = "foo";
    NiceMock<Runtime::MockLoader> runtime;
    EXPECT_CALL(runtime.snapshot_, featureEnabled("foo", 0, 5, 10000)).WillOnce(Return(true));
    EXPECT_TRUE(FilterUtility::shouldShadow(policy, runtime, 5));
  }
}

TEST_F(RouterTest, CanaryStatusTrue) {
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
                              {"x-envoy-upstream-canary", "false"},
                              {"x-envoy-virtual-cluster", "hello"}});
  ON_CALL(*cm_.conn_pool_.host_, canary()).WillByDefault(Return(true));
  response_decoder->decodeHeaders(std::move(response_headers), true);

  EXPECT_EQ(1U, stats_store_.counter("cluster.fake_cluster.canary.upstream_rq_200").value());
}

TEST_F(RouterTest, CanaryStatusFalse) {
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
                              {"x-envoy-upstream-canary", "false"},
                              {"x-envoy-virtual-cluster", "hello"}});
  response_decoder->decodeHeaders(std::move(response_headers), true);

  EXPECT_EQ(0U, stats_store_.counter("cluster.fake_cluster.canary.upstream_rq_200").value());
}

} // Router
