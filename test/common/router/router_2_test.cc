#include "source/common/tracing/http_tracer_impl.h"

#include "test/common/router/router_test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {

using testing::AtLeast;
using testing::Eq;
using testing::StartsWith;

class RouterTestSuppressEnvoyHeaders : public RouterTestBase {
public:
  RouterTestSuppressEnvoyHeaders()
      : RouterTestBase(false, true, false, Protobuf::RepeatedPtrField<std::string>{}) {}
};

// We don't get x-envoy-expected-rq-timeout-ms or an indication to insert
// x-envoy-original-path in the basic upstream test when Envoy header
// suppression is configured.
TEST_F(RouterTestSuppressEnvoyHeaders, Http1Upstream) {
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, absl::optional<Http::Protocol>(), _));
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Return(&cancellable_));
  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.route_->route_entry_, finalizeRequestHeaders(_, _, false));
  router_.decodeHeaders(headers, true);
  EXPECT_FALSE(headers.has("x-envoy-expected-rq-timeout-ms"));

  // When the router filter gets reset we should cancel the pool request.
  EXPECT_CALL(cancellable_, cancel(_));
  router_.onDestroy();
  EXPECT_TRUE(verifyHostUpstreamStats(0, 0));
  EXPECT_EQ(0U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
}

// Validate that we don't set x-envoy-overloaded when Envoy header suppression
// is enabled.
TEST_F(RouterTestSuppressEnvoyHeaders, MaintenanceMode) {
  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, maintenanceMode()).WillOnce(Return(true));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "16"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

// Validate that x-envoy-upstream-service-time is not added when Envoy header
// suppression is enabled.
// TODO(htuch): Probably should be TEST_P with
// RouterTest.EnvoyUpstreamServiceTime, this is getting verbose..
TEST_F(RouterTestSuppressEnvoyHeaders, EnvoyUpstreamServiceTime) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putHttpResponseCode(200));
  Http::TestResponseHeaderMapImpl downstream_response_headers{
      {":status", "200"}, {"x-envoy-upstream-service-time", "0"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(_, true))
      .WillOnce(Invoke([](Http::HeaderMap& headers, bool) {
        EXPECT_TRUE(headers.get(Http::Headers::get().EnvoyUpstreamServiceTime).empty());
      }));
  response_decoder->decodeHeaders(std::move(response_headers), true);
  EXPECT_TRUE(verifyHostUpstreamStats(1, 0));
}

// Validate that we don't set x-envoy-attempt-count in responses before an upstream attempt is made.
TEST_F(RouterTestSuppressEnvoyHeaders, EnvoyAttemptCountInResponseNotPresent) {
  setIncludeAttemptCountInResponse(true);

  EXPECT_CALL(*cm_.thread_local_cluster_.cluster_.info_, maintenanceMode()).WillOnce(Return(true));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "16"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(callbacks_.stream_info_, setResponseFlag(StreamInfo::ResponseFlag::UpstreamOverflow));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
}

class WatermarkTest : public RouterTestBase {
public:
  WatermarkTest() : RouterTestBase(false, false, false, Protobuf::RepeatedPtrField<std::string>{}) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  };

  void sendRequest(bool header_only_request = true, bool pool_ready = true) {
    EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
        .WillOnce(Return(std::chrono::milliseconds(0)));
    EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

    EXPECT_CALL(stream_, addCallbacks(_))
        .Times(num_add_callbacks_)
        .WillOnce(
            Invoke([&](Http::StreamCallbacks& callbacks) { stream_callbacks_ = &callbacks; }));
    EXPECT_CALL(encoder_, getStream()).WillRepeatedly(ReturnRef(stream_));
    EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
        .WillOnce(
            Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                       const Http::ConnectionPool::Instance::StreamOptions&)
                       -> Http::ConnectionPool::Cancellable* {
              response_decoder_ = &decoder;
              pool_callbacks_ = &callbacks;
              if (pool_ready) {
                callbacks.onPoolReady(encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                      upstream_stream_info_, Http::Protocol::Http10);
              }
              return nullptr;
            }));
    HttpTestUtility::addDefaultHeaders(headers_);
    router_.decodeHeaders(headers_, header_only_request);
    if (pool_ready) {
      EXPECT_EQ(
          1U, callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());
    }
  }
  void sendResponse() {
    response_decoder_->decodeHeaders(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}, true);
  }

  NiceMock<Http::MockRequestEncoder> encoder_;
  NiceMock<Http::MockStream> stream_;
  Http::StreamCallbacks* stream_callbacks_;
  Http::ResponseDecoder* response_decoder_ = nullptr;
  Http::TestRequestHeaderMapImpl headers_;
  Http::ConnectionPool::Callbacks* pool_callbacks_{nullptr};
  int num_add_callbacks_{1};
};

TEST_F(WatermarkTest, DownstreamWatermarks) {
  sendRequest();

  stream_callbacks_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_backed_up_total")
                    .value());
  stream_callbacks_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_drained_total")
                    .value());

  sendResponse();
}

TEST_F(WatermarkTest, UpstreamWatermarks) {
  sendRequest(false);

  response_decoder_->decodeHeaders(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}, false);

  ASSERT(callbacks_.callbacks_.begin() != callbacks_.callbacks_.end());
  Envoy::Http::DownstreamWatermarkCallbacks* watermark_callbacks = *callbacks_.callbacks_.begin();

  EXPECT_CALL(encoder_, getStream()).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(_));
  watermark_callbacks->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_paused_reading_total")
                    .value());

  EXPECT_CALL(encoder_, getStream()).WillOnce(ReturnRef(stream_));
  EXPECT_CALL(stream_, readDisable(_));
  watermark_callbacks->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_resumed_reading_total")
                    .value());

  Buffer::OwnedImpl data;
  EXPECT_CALL(encoder_, getStream()).WillOnce(ReturnRef(stream_));
  response_decoder_->decodeData(data, true);
}

TEST_F(WatermarkTest, FilterWatermarks) {
  EXPECT_CALL(callbacks_, decoderBufferLimit()).Times(AtLeast(3)).WillRepeatedly(Return(10));
  router_.setDecoderFilterCallbacks(callbacks_);
  // Send the headers sans-fin, and don't flag the pool as ready.
  sendRequest(false, false);

  // Send 10 bytes of body to fill the 10 byte buffer.
  Buffer::OwnedImpl data("1234567890");
  router_.decodeData(data, false);
  EXPECT_EQ(0u, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_backed_up_total")
                    .value());

  // Send one extra byte. This should cause the buffer to go over the limit and pause downstream
  // data.
  Buffer::OwnedImpl last_byte("!");
  router_.decodeData(last_byte, true);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_backed_up_total")
                    .value());

  // Now set up the downstream connection. The encoder will be given the buffered request body,
  // The mock invocation below drains it, and the buffer will go under the watermark limit again.
  EXPECT_EQ(0U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_drained_total")
                    .value());
  EXPECT_CALL(encoder_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> void { data.drain(data.length()); }));
  pool_callbacks_->onPoolReady(encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                               upstream_stream_info_, Http::Protocol::Http10);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_drained_total")
                    .value());

  sendResponse();
}

TEST_F(WatermarkTest, FilterWatermarksUnwound) {
  num_add_callbacks_ = 0;
  EXPECT_CALL(callbacks_, decoderBufferLimit()).Times(AtLeast(3)).WillRepeatedly(Return(10));
  router_.setDecoderFilterCallbacks(callbacks_);
  // Send the headers sans-fin, and don't flag the pool as ready.
  sendRequest(false, false);

  // Send 11 bytes of body to fill the 10 byte buffer.
  Buffer::OwnedImpl data("1234567890!");
  router_.decodeData(data, false);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_backed_up_total")
                    .value());

  // Set up a pool failure, and make sure the flow control blockage is undone.
  pool_callbacks_->onPoolFailure(Http::ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                                 absl::string_view(), nullptr);
  EXPECT_EQ(1U, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                    .counter("upstream_flow_control_drained_total")
                    .value());
}

// Same as RetryRequestNotComplete but with decodeData larger than the buffer
// limit, no retry will occur.
TEST_F(WatermarkTest, RetryRequestNotComplete) {
  EXPECT_CALL(callbacks_, decoderBufferLimit()).Times(AtLeast(2)).WillRepeatedly(Return(10));
  router_.setDecoderFilterCallbacks(callbacks_);
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder1, &response_decoder, Http::Protocol::Http10);

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRemoteReset));

  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data("1234567890123");
  EXPECT_CALL(*router_.retry_state_, enabled()).WillOnce(Return(true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _, _)).Times(0);
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _, _)).Times(0);
  // This will result in retry_state_ being deleted.
  router_.decodeData(data, false);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // This should not trigger a retry as the retry state has been deleted.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  EXPECT_EQ(callbacks_.details(), "upstream_reset_before_response_started{remote_reset}");
}

class RouterTestChildSpan : public RouterTestBase {
public:
  RouterTestChildSpan()
      : RouterTestBase(true, false, false, Protobuf::RepeatedPtrField<std::string>{}) {}
};

// Make sure child spans start/inject/finish with a normal flow.
// An upstream request succeeds and a single span is created.
TEST_F(RouterTestChildSpan, BasicFlow) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*child_span, injectContext(_, _));
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.active_span_, spawnChild_(_, "router observability_name egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(callbacks_, tracingConfig()).Times(2);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, finishSpan());
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

// Make sure child spans start/inject/finish with a reset flow.
// The upstream responds back to envoy before the reset, so the span has fields that represent a
// response and reset.
TEST_F(RouterTestChildSpan, ResetFlow) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*child_span, injectContext(_, _));
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.active_span_, spawnChild_(_, "router observability_name egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(callbacks_, tracingConfig()).Times(2);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Upstream responds back to envoy.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), false);

  // The reset occurs after the upstream response, so the span has a valid status code but also an
  // error.
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("UR")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ErrorReason), Eq("remote reset")));
  EXPECT_CALL(*child_span, finishSpan());
  encoder.stream_.resetStream(Http::StreamResetReason::RemoteReset);
}

// Make sure child spans start/inject/finish with a cancellation flow.
// An upstream request is created but is then cancelled before. The resulting span has the
// cancellation fields.
TEST_F(RouterTestChildSpan, CancelFlow) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke([&](Http::StreamDecoder&, Http::ConnectionPool::Callbacks& callbacks,
                           const Http::ConnectionPool::Instance::StreamOptions&)
                           -> Http::ConnectionPool::Cancellable* {
        EXPECT_CALL(*child_span, injectContext(_, _));
        callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                              upstream_stream_info_, Http::Protocol::Http10);
        return nullptr;
      }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.active_span_, spawnChild_(_, "router observability_name egress", _))
      .WillOnce(Return(child_span));
  EXPECT_CALL(callbacks_, tracingConfig()).Times(2);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Destroy the router, causing the upstream request to be cancelled.
  // Response code on span is 0 because the upstream never sent a response.
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Canceled), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());
  router_.onDestroy();
}

// Make sure child spans start/inject/finish with retry flow.
// The first request will fail because of an upstream reset, so the span will be annotated with the
// reset reason. The second request will succeed, so the span will be annotated with 200 OK.
TEST_F(RouterTestChildSpan, ResetRetryFlow) {
  NiceMock<Http::MockRequestEncoder> encoder1;
  Http::ResponseDecoder* response_decoder = nullptr;
  Tracing::MockSpan* child_span_1{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*child_span_1, injectContext(_, _));
            callbacks.onPoolReady(encoder1, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));
  expectResponseTimerCreate();

  // Upstream responds back to envoy simulating an upstream reset.
  Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"}, {"x-envoy-internal", "true"}};
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(callbacks_.active_span_, spawnChild_(_, "router observability_name egress", _))
      .WillOnce(Return(child_span_1));
  EXPECT_CALL(callbacks_, tracingConfig()).Times(2);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // The span should be annotated with the reset-related fields.
  EXPECT_CALL(*child_span_1,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span_1,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("UR")));
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)))
      .Times(2);
  EXPECT_CALL(*child_span_1, setTag(Eq(Tracing::Tags::get().ErrorReason), Eq("remote reset")));
  EXPECT_CALL(*child_span_1, finishSpan());

  router_.retry_state_->expectResetRetry();
  encoder1.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  // We expect this reset to kick off a new request.
  NiceMock<Http::MockRequestEncoder> encoder2;
  Tracing::MockSpan* child_span_2{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(*child_span_2, injectContext(_, _));
            EXPECT_CALL(*router_.retry_state_, onHostAttempted(_));
            callbacks.onPoolReady(encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  EXPECT_CALL(callbacks_.active_span_, spawnChild_(_, "router observability_name egress", _))
      .WillOnce(Return(child_span_2));
  EXPECT_CALL(callbacks_, tracingConfig()).Times(2);
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().RetryCount), Eq("1")));

  router_.retry_state_->callback_();
  EXPECT_EQ(2U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  // Upstream responds back with a normal response. Span should be annotated as usual.
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  EXPECT_CALL(*child_span_2,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.0")));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.5:9211")));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span_2,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span_2, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span_2, finishSpan());
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

class RouterTestNoChildSpan : public RouterTestBase {
public:
  RouterTestNoChildSpan()
      : RouterTestBase(false, false, false, Protobuf::RepeatedPtrField<std::string>{}) {}
};

TEST_F(RouterTestNoChildSpan, BasicFlow) {
  EXPECT_CALL(callbacks_.route_->route_entry_, timeout())
      .WillOnce(Return(std::chrono::milliseconds(0)));
  EXPECT_CALL(callbacks_.dispatcher_, createTimer_(_)).Times(0);

  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            EXPECT_CALL(callbacks_.active_span_, injectContext(_, _));
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  ASSERT(response_decoder);
  response_decoder->decodeHeaders(std::move(response_headers), true);
}

namespace {

Protobuf::RepeatedPtrField<std::string> protobufStrList(const std::vector<std::string>& v) {
  Protobuf::RepeatedPtrField<std::string> res;
  for (auto& field : v) {
    *res.Add() = field;
  }

  return res;
}

} // namespace

class RouterTestStrictCheckOneHeader : public RouterTestBase,
                                       public testing::WithParamInterface<std::string> {
public:
  RouterTestStrictCheckOneHeader()
      : RouterTestBase(false, false, false, protobufStrList({GetParam()})){};
};

INSTANTIATE_TEST_SUITE_P(StrictHeaderCheck, RouterTestStrictCheckOneHeader,
                         testing::Values("x-envoy-upstream-rq-timeout-ms",
                                         "x-envoy-upstream-rq-per-try-timeout-ms",
                                         "x-envoy-max-retries", "x-envoy-retry-on",
                                         "x-envoy-retry-grpc-on"));

// Each test param instantiates a router that strict-checks one particular header.
// This test decodes a set of headers with invalid values and asserts that the
// strict header check only fails for the single header specified by the test param
TEST_P(RouterTestStrictCheckOneHeader, SingleInvalidHeader) {
  Http::TestRequestHeaderMapImpl req_headers{
      {"X-envoy-Upstream-rq-timeout-ms", "10.0"},
      {"x-envoy-upstream-rq-per-try-timeout-ms", "1.0"},
      {"x-envoy-max-retries", "2.0"},
      {"x-envoy-retry-on", "5xx,cancelled"},                // 'cancelled' is an invalid entry
      {"x-envoy-retry-grpc-on", "5xx,cancelled, internal"}, // '5xx' is an invalid entry
  };
  HttpTestUtility::addDefaultHeaders(req_headers);
  auto checked_header = GetParam();

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& response_headers, bool end_stream) -> void {
        EXPECT_EQ(enumToInt(Http::Code::BadRequest),
                  Envoy::Http::Utility::getResponseStatus(response_headers));
        EXPECT_FALSE(end_stream);
      }));

  EXPECT_CALL(callbacks_, encodeData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool end_stream) -> void {
        EXPECT_THAT(data.toString(),
                    StartsWith(fmt::format("invalid header '{}' with value ", checked_header)));
        EXPECT_TRUE(end_stream);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, router_.decodeHeaders(req_headers, true));
  EXPECT_EQ(callbacks_.details(),
            fmt::format("request_headers_failed_strict_check{{{}}}", checked_header));
}

class RouterTestStrictCheckSomeHeaders
    : public RouterTestBase,
      public testing::WithParamInterface<std::vector<std::string>> {
public:
  RouterTestStrictCheckSomeHeaders()
      : RouterTestBase(false, false, false, protobufStrList(GetParam())){};
};

INSTANTIATE_TEST_SUITE_P(StrictHeaderCheck, RouterTestStrictCheckSomeHeaders,
                         testing::Values(std::vector<std::string>{"x-envoy-upstream-rq-timeout-ms",
                                                                  "x-envoy-max-retries"},
                                         std::vector<std::string>{}));

// Request has headers with invalid values, but headers are *excluded* from the
// set to which strict-checks apply. Assert that these headers are not rejected.
TEST_P(RouterTestStrictCheckSomeHeaders, IgnoreOmittedHeaders) {
  // Invalid, but excluded from the configured set of headers to strictly-check
  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-upstream-rq-per-try-timeout-ms", "1.0"},
      {"x-envoy-upstream-rq-timeout-ms", "5000"},
      {"x-envoy-retry-on", "5xx,cancelled"},
  };
  HttpTestUtility::addDefaultHeaders(headers);

  expectResponseTimerCreate();
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, router_.decodeHeaders(headers, true));
  router_.onDestroy();
}

const std::vector<std::string> SUPPORTED_STRICT_CHECKED_HEADERS = {
    "x-envoy-upstream-rq-timeout-ms", "x-envoy-upstream-rq-per-try-timeout-ms", "x-envoy-retry-on",
    "x-envoy-retry-grpc-on", "x-envoy-max-retries"};

class RouterTestStrictCheckAllHeaders
    : public RouterTestBase,
      public testing::WithParamInterface<std::tuple<std::string, std::string>> {
public:
  RouterTestStrictCheckAllHeaders()
      : RouterTestBase(false, false, false, protobufStrList(SUPPORTED_STRICT_CHECKED_HEADERS)){};
};

INSTANTIATE_TEST_SUITE_P(StrictHeaderCheck, RouterTestStrictCheckAllHeaders,
                         testing::Combine(testing::ValuesIn(SUPPORTED_STRICT_CHECKED_HEADERS),
                                          testing::ValuesIn(SUPPORTED_STRICT_CHECKED_HEADERS)));

// Each instance of this test configures a router to strict-validate all
// supported headers and asserts that a request with invalid values set for some
// *pair* of headers is rejected.
TEST_P(RouterTestStrictCheckAllHeaders, MultipleInvalidHeaders) {
  const auto& header1 = std::get<0>(GetParam());
  const auto& header2 = std::get<1>(GetParam());
  Http::TestRequestHeaderMapImpl headers{{header1, "invalid"}, {header2, "invalid"}};
  HttpTestUtility::addDefaultHeaders(headers);

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders));

  EXPECT_CALL(callbacks_, encodeHeaders_(_, _))
      .WillOnce(Invoke([&](Http::ResponseHeaderMap& response_headers, bool end_stream) -> void {
        EXPECT_EQ(enumToInt(Http::Code::BadRequest),
                  Envoy::Http::Utility::getResponseStatus(response_headers));
        EXPECT_FALSE(end_stream);
      }));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, router_.decodeHeaders(headers, true));
  EXPECT_THAT(callbacks_.details(),
              StartsWith(fmt::format("request_headers_failed_strict_check{{")));
  router_.onDestroy();
}

// Request has headers with invalid values, but headers are *excluded* from the
// set to which strict-checks apply. Assert that these headers are not rejected.
TEST(RouterFilterUtilityTest, StrictCheckValidHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {"X-envoy-Upstream-rq-timeout-ms", "100"},
      {"x-envoy-upstream-rq-per-try-timeout-ms", "100"},
      {"x-envoy-max-retries", "2"},
      {"not-checked", "always passes"},
      {"x-envoy-retry-on", "5xx,gateway-error,retriable-4xx,refused-stream,connect-failure,"
                           "retriable-status-codes , reset"}, // space is allowed
      {"x-envoy-retry-grpc-on",
       "cancelled,internal,deadline-exceeded,resource-exhausted , unavailable"}, // space is allowed
  };

  for (const auto& target : SUPPORTED_STRICT_CHECKED_HEADERS) {
    EXPECT_TRUE(
        FilterUtility::StrictHeaderChecker::checkHeader(headers, Http::LowerCaseString(target))
            .valid_)
        << fmt::format("'{}' should have passed strict validation", target);
  }

  Http::TestRequestHeaderMapImpl failing_headers{
      {"X-envoy-Upstream-rq-timeout-ms", "10.0"},
      {"x-envoy-upstream-rq-per-try-timeout-ms", "1.0"},
      {"x-envoy-max-retries", "2.0"},
      {"x-envoy-retry-on", "5xx,cancelled"},                // 'cancelled' is an invalid entry
      {"x-envoy-retry-grpc-on", "5xx,cancelled, internal"}, // '5xx' is an invalid entry
  };

  for (const auto& target : SUPPORTED_STRICT_CHECKED_HEADERS) {
    EXPECT_FALSE(FilterUtility::StrictHeaderChecker::checkHeader(failing_headers,
                                                                 Http::LowerCaseString(target))
                     .valid_)
        << fmt::format("'{}' should have failed strict validation", target);
  }
}

class RouterTestSupressGRPCStatsEnabled : public RouterTestBase {
public:
  RouterTestSupressGRPCStatsEnabled()
      : RouterTestBase(false, false, true, Protobuf::RepeatedPtrField<std::string>{}) {}
};

TEST_F(RouterTestSupressGRPCStatsEnabled, ExcludeTimeoutHttpStats) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(
          Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                     const Http::ConnectionPool::Instance::StreamOptions&)
                     -> Http::ConnectionPool::Cancellable* {
            response_decoder = &decoder;
            callbacks.onPoolReady(encoder, cm_.thread_local_cluster_.conn_pool_.host_,
                                  upstream_stream_info_, Http::Protocol::Http10);
            return nullptr;
          }));

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-internal", "true"}, {"content-type", "application/grpc"}, {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _, _)).Times(0);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  response_timeout_->invokeCallback();

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_timeout_.value());
  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_completed")
                .value());
  EXPECT_EQ(
      0U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
  EXPECT_EQ(
      0U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_5xx").value());
}

class RouterTestSupressGRPCStatsDisabled : public RouterTestBase {
public:
  RouterTestSupressGRPCStatsDisabled()
      : RouterTestBase(false, false, false, Protobuf::RepeatedPtrField<std::string>{}) {}
};

TEST_F(RouterTestSupressGRPCStatsDisabled, IncludeHttpTimeoutStats) {
  NiceMock<Http::MockRequestEncoder> encoder;
  Http::ResponseDecoder* response_decoder = nullptr;
  expectNewStreamWithImmediateEncoder(encoder, &response_decoder, Http::Protocol::Http10);

  expectResponseTimerCreate();

  Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-internal", "true"}, {"content-type", "application/grpc"}, {"grpc-timeout", "20S"}};
  HttpTestUtility::addDefaultHeaders(headers);
  router_.decodeHeaders(headers, false);
  Buffer::OwnedImpl data;
  router_.decodeData(data, true);
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_total_.value());

  EXPECT_CALL(callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::UpstreamRequestTimeout));
  EXPECT_CALL(encoder.stream_, resetStream(Http::StreamResetReason::LocalReset));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  EXPECT_CALL(*router_.retry_state_, shouldRetryReset(_, _, _)).Times(0);
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_.host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  response_timeout_->invokeCallback();

  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(1U,
            callbacks_.route_->route_entry_.virtual_cluster_.stats().upstream_rq_timeout_.value());

  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
  EXPECT_EQ(
      1U,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_5xx").value());
  EXPECT_EQ(1U,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_completed")
                .value());
}

} // namespace Router
} // namespace Envoy
