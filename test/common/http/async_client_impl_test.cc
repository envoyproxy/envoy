#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/router/context_impl.h"
#include "source/common/router/upstream_codec_filter.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {

class AsyncClientImplTest : public testing::Test {
public:
  AsyncClientImplTest()
      : http_context_(stats_store_.symbolTable()), router_context_(stats_store_.symbolTable()),
        client_(cm_.thread_local_cluster_.cluster_.info_, stats_store_, dispatcher_, local_info_,
                cm_, runtime_, random_,
                Router::ShadowWriterPtr{new NiceMock<Router::MockShadowWriter>()}, http_context_,
                router_context_) {
    message_->headers().setMethod("GET");
    message_->headers().setHost("host");
    message_->headers().setPath("/");
    ON_CALL(*cm_.thread_local_cluster_.conn_pool_.host_, locality())
        .WillByDefault(ReturnRef(envoy::config::core::v3::Locality().default_instance()));
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    HttpTestUtility::addDefaultHeaders(headers_);
  }

  virtual void expectSuccess(AsyncClient::Request* sent_request, uint64_t code) {
    EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
    EXPECT_CALL(callbacks_, onSuccess_(_, _))
        .WillOnce(Invoke([sent_request, code](const AsyncClient::Request& request,
                                              ResponseMessage* response) -> void {
          // Verify that callback is called with the same request handle as returned by
          // AsyncClient::send().
          EXPECT_EQ(sent_request, &request);
          EXPECT_EQ(code, Utility::getResponseStatus(response->headers()));
        }));
  }

  void expectResponseHeaders(MockAsyncClientStreamCallbacks& callbacks, uint64_t code,
                             bool end_stream) {
    EXPECT_CALL(callbacks, onHeaders_(_, end_stream))
        .WillOnce(Invoke([code](ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ(std::to_string(code), headers.getStatusValue());
        }));
  }

  TestRequestHeaderMapImpl headers_{};
  RequestMessagePtr message_{new RequestMessageImpl()};
  Stats::MockIsolatedStatsStore stats_store_;
  NiceMock<MockAsyncClientCallbacks> callbacks_;
  NiceMock<MockAsyncClientStreamCallbacks> stream_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockRequestEncoder> stream_encoder_;
  ResponseDecoder* response_decoder_{};
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  AsyncClientImpl client_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class AsyncClientImplTracingTest : public AsyncClientImplTest {
public:
  AsyncClientImplTracingTest() {
    ON_CALL(stream_info_, upstreamClusterInfo())
        .WillByDefault(Return(absl::make_optional<Upstream::ClusterInfoConstSharedPtr>(
            cm_.thread_local_cluster_.cluster_.info_)));
  }

  Tracing::MockSpan parent_span_;
  const std::string child_span_name_{"Test Child Span Name"};

  void expectSuccess(AsyncClient::Request* sent_request, uint64_t code) override {
    EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _))
        .WillOnce(Invoke([](Tracing::Span& span, const Http::ResponseHeaderMap* response_headers) {
          span.setTag("onBeforeFinalizeUpstreamSpan", "called");
          ASSERT_NE(nullptr, response_headers);
        }));
    EXPECT_CALL(callbacks_, onSuccess_(_, _))
        .WillOnce(Invoke([sent_request, code](const AsyncClient::Request& request,
                                              ResponseMessage* response) -> void {
          // Verify that callback is called with the same request handle as returned by
          // AsyncClient::send().
          EXPECT_EQ(sent_request, &request);
          EXPECT_EQ(code, Utility::getResponseStatus(response->headers()));
        }));
  }
};

TEST_F(AsyncClientImplTest, BasicStream) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            // The backing object of 'decoder' should also implemented the
            // Router::UpstreamToDownstream.
            const auto* upstream_to_downstream =
                dynamic_cast<Router::UpstreamToDownstream*>(&decoder);
            EXPECT_NE(nullptr, upstream_to_downstream);
            // Ensure the route() is populated and valid.
            EXPECT_NE(nullptr, upstream_to_downstream->route().routeEntry());

            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());

  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  response_decoder_->decode1xxHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "100"}}));
  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, Basic) {
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&copy), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, BasicOngoingRequest) {
  auto headers = std::make_unique<TestRequestHeaderMapImpl>();
  HttpTestUtility::addDefaultHeaders(*headers);
  TestRequestHeaderMapImpl headers_copy = *headers;

  Buffer::OwnedImpl data("test data");
  const Buffer::OwnedImpl data_copy(data.toString());

  auto trailers = std::make_unique<TestRequestTrailerMapImpl>();
  trailers->addCopy("some", "trailer");
  const TestRequestTrailerMapImpl trailers_copy = *trailers;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  headers_copy.addCopy("x-envoy-internal", "true");
  headers_copy.addCopy("x-forwarded-for", "127.0.0.1");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers_copy), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data_copy), false));
  EXPECT_CALL(stream_encoder_, encodeTrailers(HeaderMapEqualRef(&trailers_copy)));

  AsyncClient::OngoingRequest* request =
      client_.startRequest(std::move(headers), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  request->sendData(data, false);
  request->captureAndSendTrailers(std::move(trailers));

  expectSuccess(request, 200);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  Buffer::OwnedImpl response_data("test data");
  response_decoder_->decodeData(response_data, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, OngoingRequestWithWatermarking) {
  auto headers = std::make_unique<TestRequestHeaderMapImpl>();
  HttpTestUtility::addDefaultHeaders(*headers);
  TestRequestHeaderMapImpl headers_copy = *headers;
  headers_copy.addCopy("x-envoy-internal", "true");
  headers_copy.addCopy("x-forwarded-for", "127.0.0.1");

  Buffer::OwnedImpl data("test data");
  const Buffer::OwnedImpl data_copy(data.toString());

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            // Pretend like the connection is already backed up.
            dynamic_cast<MockStream&>(stream_encoder_.getStream()).runHighWatermarkCallbacks();
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers_copy), false));

  auto* request =
      client_.startRequest(std::move(headers), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);
  StrictMock<MockStreamDecoderFilterCallbacks> watermark_callbacks;
  // Registering a new watermark callback should note that the high watermark has already been hit.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterAboveWriteBufferHighWatermark());
  request->setWatermarkCallbacks(watermark_callbacks);

  // Upstream gets unblocked.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterBelowWriteBufferLowWatermark());
  dynamic_cast<MockStream&>(stream_encoder_.getStream()).runLowWatermarkCallbacks();

  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data_copy), false));
  request->sendData(data, false);
  // Blocked again.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterAboveWriteBufferHighWatermark());
  dynamic_cast<MockStream&>(stream_encoder_.getStream()).runHighWatermarkCallbacks();

  // Clear the callback, which calls the low watermark function.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterBelowWriteBufferLowWatermark());
  request->removeWatermarkCallbacks();
  // Add the callback back.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterAboveWriteBufferHighWatermark());
  request->setWatermarkCallbacks(watermark_callbacks);

  EXPECT_CALL(stream_encoder_, encodeData(BufferStringEqual(""), true));
  Buffer::OwnedImpl empty;
  request->sendData(empty, true);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  // On request end, we expect to run the low watermark callbacks.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterBelowWriteBufferLowWatermark());
  response_decoder_->decodeHeaders(std::move(response_headers), true);
}

TEST_F(AsyncClientImplTest, OngoingRequestWithWatermarkingAndReset) {
  auto headers = std::make_unique<TestRequestHeaderMapImpl>();
  HttpTestUtility::addDefaultHeaders(*headers);
  TestRequestHeaderMapImpl headers_copy = *headers;
  headers_copy.addCopy("x-envoy-internal", "true");
  headers_copy.addCopy("x-forwarded-for", "127.0.0.1");

  Buffer::OwnedImpl data("test data");
  const Buffer::OwnedImpl data_copy(data.toString());

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers_copy), false));

  auto* request =
      client_.startRequest(std::move(headers), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  StrictMock<MockStreamDecoderFilterCallbacks> watermark_callbacks;
  request->setWatermarkCallbacks(watermark_callbacks);

  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data_copy), false));
  request->sendData(data, false);
  // Upstream is blocked.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterAboveWriteBufferHighWatermark());
  dynamic_cast<MockStream&>(stream_encoder_.getStream()).runHighWatermarkCallbacks();

  // Reset the stream, which will call the low watermark callbacks.
  EXPECT_CALL(watermark_callbacks, onDecoderFilterBelowWriteBufferLowWatermark());
  expectSuccess(request, 503);
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTracingTest, Basic) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(parent_span_, spawnChild_(_, "async fake_cluster egress", _))
      .WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions().setParentSpan(parent_span_);
  EXPECT_CALL(*child_span, setSampled(true));
  EXPECT_CALL(*child_span, injectContext(_, _));

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  EXPECT_CALL(*child_span, setTag(Eq("onBeforeFinalizeUpstreamSpan"), Eq("called")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, finishSpan());

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);

  EXPECT_TRUE(stats_store_.findCounterByString("http.async-client.rq_total").has_value());
}

TEST_F(AsyncClientImplTracingTest, BasicNamedChildSpan) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(parent_span_, spawnChild_(_, child_span_name_, _)).WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions()
                                            .setParentSpan(parent_span_)
                                            .setChildSpanName(child_span_name_)
                                            .setSampled(false);
  EXPECT_CALL(*child_span, setSampled(false));
  EXPECT_CALL(*child_span, injectContext(_, _));

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  EXPECT_CALL(*child_span, setTag(Eq("onBeforeFinalizeUpstreamSpan"), Eq("called")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, finishSpan());

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTracingTest, BasicNamedChildSpanKeepParentSampling) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(parent_span_, spawnChild_(_, child_span_name_, _)).WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions()
                                            .setParentSpan(parent_span_)
                                            .setChildSpanName(child_span_name_)
                                            .setSampled(absl::nullopt);
  EXPECT_CALL(*child_span, setSampled(_)).Times(0);
  EXPECT_CALL(*child_span, injectContext(_, _));

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  EXPECT_CALL(*child_span, setTag(Eq("onBeforeFinalizeUpstreamSpan"), Eq("called")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("200")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, finishSpan());

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, BasicHashPolicy) {
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));
  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(
          Invoke([&](Upstream::ResourcePriority, auto, Upstream::LoadBalancerContext* context) {
            // this is the hash of :path header value "/"
            // the hash stability across releases is expected, so test the hash value directly here.
            EXPECT_EQ(16761507700594825962UL, context->computeHashKey().value());
            return Upstream::HttpPoolData([]() {}, &cm_.thread_local_cluster_.conn_pool_);
          }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&copy), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  AsyncClient::RequestOptions options;
  Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy> hash_policy;
  hash_policy.Add()->mutable_header()->set_header_name(":path");
  options.setHashPolicy(hash_policy);

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, WithoutMetadata) {
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                           Upstream::LoadBalancerContext* context) {
        EXPECT_EQ(context->metadataMatchCriteria(), nullptr);
        return Upstream::HttpPoolData([]() {}, &cm_.thread_local_cluster_.conn_pool_);
      }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&copy), false));

  AsyncClient::RequestOptions options;
  envoy::config::core::v3::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      {"fake_test_domain", MessageUtil::keyValueStruct("fake_test_key", "fake_test_value")});
  options.setMetadata(metadata);

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, WithMetadata) {
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(cm_.thread_local_cluster_, httpConnPool(_, _, _))
      .WillOnce(Invoke([&](Upstream::ResourcePriority, absl::optional<Http::Protocol>,
                           Upstream::LoadBalancerContext* context) {
        EXPECT_NE(context->metadataMatchCriteria(), nullptr);
        EXPECT_EQ(context->metadataMatchCriteria()->metadataMatchCriteria().at(0)->name(),
                  "fake_test_key");
        return Upstream::HttpPoolData([]() {}, &cm_.thread_local_cluster_.conn_pool_);
      }));

  TestRequestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&copy), false));

  AsyncClient::RequestOptions options;
  envoy::config::core::v3::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      {Envoy::Config::MetadataFilters::get().ENVOY_LB,
       MessageUtil::keyValueStruct("fake_test_key", "fake_test_value")});
  options.setMetadata(metadata);

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, Retry) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));
  RequestMessage* message_copy = message_.get();

  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  message_->headers().setReferenceEnvoyRetryOn(Headers::get().EnvoyRetryOnValues._5xx);

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_copy->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  timer_->invokeCallback();

  // Normal response.
  expectSuccess(request, 200);
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
}

TEST_F(AsyncClientImplTest, RetryWithStream) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  headers.setReferenceEnvoyRetryOn(Headers::get().EnvoyRetryOnValues._5xx);
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, AsyncClient::StreamOptions().setBufferBodyForRetry(true));
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));
  timer_->invokeCallback();

  // Normal response.
  expectResponseHeaders(stream_callbacks_, 200, true);
  EXPECT_CALL(stream_callbacks_, onComplete());
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(AsyncClientImplTest, DataBufferForRetryOverflow) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));

  // large body must be > 64KB
  Buffer::InstancePtr large_body{new Buffer::OwnedImpl(std::string((1 << 16) + 1, 'a'))};

  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(large_body.get()), true));

  headers.setReferenceEnvoyRetryOn(Headers::get().EnvoyRetryOnValues._5xx);
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, AsyncClient::StreamOptions().setBufferBodyForRetry(true));
  stream->sendHeaders(headers, false);
  stream->sendData(*large_body, true);

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));

  // On retry, data will be empty because it was larger than > 64KB
  Buffer::InstancePtr empty_buffer{new Buffer::OwnedImpl("")};
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(empty_buffer.get()), true));
  timer_->invokeCallback();

  // Normal response.
  expectResponseHeaders(stream_callbacks_, 200, true);
  EXPECT_CALL(stream_callbacks_, onComplete());
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(AsyncClientImplTest, MultipleStreams) {
  // Start stream 1
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers(message_->headers());
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Start stream 2
  Buffer::InstancePtr body2{new Buffer::OwnedImpl("test body")};
  NiceMock<MockRequestEncoder> stream_encoder2;
  ResponseDecoder* response_decoder2{};
  MockAsyncClientStreamCallbacks stream_callbacks2;

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder2 = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers2(message_->headers());
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&headers2), false));
  EXPECT_CALL(stream_encoder2, encodeData(BufferEqual(body2.get()), true));

  expectResponseHeaders(stream_callbacks2, 503, true);
  EXPECT_CALL(stream_callbacks2, onComplete());

  AsyncClient::Stream* stream2 = client_.start(stream_callbacks2, AsyncClient::StreamOptions());
  stream2->sendHeaders(headers2, false);
  stream2->sendData(*body2, true);

  // Finish stream 2.
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "503"}});
  ASSERT(response_decoder2);
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish stream 1.
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(*body, true);
}

TEST_F(AsyncClientImplTest, MultipleRequests) {
  // Send request 1
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  auto* request1 = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request1, nullptr);

  // Send request 2.
  RequestMessagePtr message2{new RequestMessageImpl()};
  HttpTestUtility::addDefaultHeaders(message2->headers());
  NiceMock<MockRequestEncoder> stream_encoder2;
  ResponseDecoder* response_decoder2{};
  MockAsyncClientCallbacks callbacks2;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder2 = &decoder;
            return nullptr;
          }));
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&message2->headers()), true));

  auto* request2 = client_.send(std::move(message2), callbacks2, AsyncClient::RequestOptions());
  EXPECT_NE(request2, nullptr);

  // Send request 3.
  RequestMessagePtr message3{new RequestMessageImpl()};
  HttpTestUtility::addDefaultHeaders(message3->headers());
  NiceMock<MockRequestEncoder> stream_encoder3;
  ResponseDecoder* response_decoder3{};
  MockAsyncClientCallbacks callbacks3;
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder3, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder3 = &decoder;
            return nullptr;
          }));
  EXPECT_CALL(stream_encoder3, encodeHeaders(HeaderMapEqualRef(&message3->headers()), true));

  auto* request3 = client_.send(std::move(message3), callbacks3, AsyncClient::RequestOptions());
  EXPECT_NE(request3, nullptr);

  // Finish request 2.
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(callbacks2, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks2, onSuccess_(_, _))
      .WillOnce(Invoke(
          [request2](const AsyncClient::Request& request, ResponseMessage* response) -> void {
            // Verify that callback is called with the same request handle as returned by
            // AsyncClient::send().
            EXPECT_EQ(request2, &request);
            EXPECT_EQ(503, Utility::getResponseStatus(response->headers()));
          }));
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish request 1.
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  expectSuccess(request1, 200);
  response_decoder_->decodeData(data, true);

  // Finish request 3.
  ResponseHeaderMapPtr response_headers3(new TestResponseHeaderMapImpl{{":status", "500"}});
  EXPECT_CALL(callbacks3, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks3, onSuccess_(_, _))
      .WillOnce(Invoke(
          [request3](const AsyncClient::Request& request, ResponseMessage* response) -> void {
            // Verify that callback is called with the same request handle as returned by
            // AsyncClient::send().
            EXPECT_EQ(request3, &request);
            EXPECT_EQ(500, Utility::getResponseStatus(response->headers()));
          }));
  response_decoder3->decodeHeaders(std::move(response_headers3), true);
}

TEST_F(AsyncClientImplTest, StreamAndRequest) {
  // Send request
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  // Start stream
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  NiceMock<MockRequestEncoder> stream_encoder2;
  ResponseDecoder* response_decoder2{};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder2, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder2 = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder2, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Finish stream.
  ResponseHeaderMapPtr response_headers2(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder2->decodeHeaders(std::move(response_headers2), false);
  response_decoder2->decodeData(*body, true);

  // Finish request.
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  expectSuccess(request, 200);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, StreamWithTrailers) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  TestRequestTrailerMapImpl trailers{{"some", "request_trailer"}};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_encoder_, encodeTrailers(HeaderMapEqualRef(&trailers)));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  TestResponseTrailerMapImpl expected_trailers{{"some", "trailer"}};
  EXPECT_CALL(stream_callbacks_, onTrailers_(HeaderMapEqualRef(&expected_trailers)));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);
  stream->sendTrailers(trailers);

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(*body, false);
  response_decoder_->decodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, Trailers) {
  message_->body().add("test body");
  Buffer::Instance& data = message_->body();

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 200);
  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, false);
  response_decoder_->decodeTrailers(
      ResponseTrailerMapPtr{new TestResponseTrailerMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, ImmediateReset) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 503);
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, LocalResetAfterStreamStart) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  stream->reset();
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(AsyncClientImplTest, SendDataAfterRemoteClosure) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);

  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, true);

  EXPECT_CALL(stream_encoder_, encodeData(_, _)).Times(0);
  stream->sendData(*body, true);
}

TEST_F(AsyncClientImplTest, SendTrailersRemoteClosure) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  TestRequestTrailerMapImpl trailers;
  trailers.addCopy("x-test-trailer", "1");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);

  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, true);

  EXPECT_CALL(stream_encoder_, encodeTrailers(_)).Times(0);
  stream->sendTrailers(trailers);
}

// Validate behavior when the stream's onHeaders() callback performs a stream
// reset.
TEST_F(AsyncClientImplTest, ResetInOnHeaders) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false))
      .WillOnce(Invoke([&stream](HeaderMap&, bool) { stream->reset(); }));
  EXPECT_CALL(stream_callbacks_, onData(_, _)).Times(0);
  EXPECT_CALL(stream_callbacks_, onReset());

  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      dynamic_cast<Http::AsyncStreamImpl*>(stream);
  filter_callbacks->encodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false, "details");
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(AsyncClientImplTest, RemoteResetAfterStreamStart) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(AsyncClientImplTest, ResetAfterResponseStart) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks_, onFailure(_, _))
      .WillOnce(Invoke([sent_request = request](const AsyncClient::Request& request,
                                                AsyncClient::FailureReason reason) {
        // Verify that callback is called with the same request handle as returned by
        // AsyncClient::send().
        EXPECT_EQ(&request, sent_request);
        EXPECT_EQ(reason, AsyncClient::FailureReason::Reset);
      }));

  ResponseHeaderMapPtr response_headers(new TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTest, ResetStream) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(message_->headers(), true);
  stream->reset();
}

TEST_F(AsyncClientImplTest, CancelRequest) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  request->cancel();
}

TEST_F(AsyncClientImplTracingTest, CancelRequest) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(parent_span_, spawnChild_(_, "async fake_cluster egress", _))
      .WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions().setParentSpan(parent_span_);
  EXPECT_CALL(*child_span, setSampled(true));
  EXPECT_CALL(*child_span, injectContext(_, _));
  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _))
      .WillOnce(Invoke([](Tracing::Span& span, const Http::ResponseHeaderMap* response_headers) {
        span.setTag("onBeforeFinalizeUpstreamSpan", "called");
        // Since this is  a failure, we expect no response headers.
        ASSERT_EQ(nullptr, response_headers);
      }));
  AsyncClient::Request* request = client_.send(std::move(message_), callbacks_, options);

  EXPECT_CALL(*child_span, setTag(Eq("onBeforeFinalizeUpstreamSpan"), Eq("called")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));

  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Canceled), Eq(Tracing::Tags::get().True)));
  EXPECT_CALL(*child_span, finishSpan());

  request->cancel();
}

TEST_F(AsyncClientImplTest, DestroyWithActiveStream) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());
  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(message_->headers(), false);
}

TEST_F(AsyncClientImplTest, DestroyWithActiveRequest) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));

  auto* request = client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions());
  EXPECT_NE(request, nullptr);

  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks_, onFailure(_, _))
      .WillOnce(Invoke([sent_request = request](const AsyncClient::Request& request,
                                                AsyncClient::FailureReason reason) {
        // Verify that callback is called with the same request handle as returned by
        // AsyncClient::send().
        EXPECT_EQ(&request, sent_request);
        EXPECT_EQ(reason, AsyncClient::FailureReason::Reset);
      }));
}

TEST_F(AsyncClientImplTracingTest, DestroyWithActiveRequest) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(parent_span_, spawnChild_(_, "async fake_cluster egress", _))
      .WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions().setParentSpan(parent_span_);
  EXPECT_CALL(*child_span, setSampled(true));
  EXPECT_CALL(*child_span, injectContext(_, _));

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks_, onFailure(_, _))
      .WillOnce(Invoke([sent_request = request](const AsyncClient::Request& request,
                                                AsyncClient::FailureReason reason) {
        // Verify that callback is called with the same request handle as returned by
        // AsyncClient::send().
        EXPECT_EQ(&request, sent_request);
        EXPECT_EQ(reason, AsyncClient::FailureReason::Reset);
      }));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("0")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("-")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)))
      .Times(AnyNumber());
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ErrorReason), Eq("Reset")));
  EXPECT_CALL(*child_span, finishSpan());
}

TEST_F(AsyncClientImplTest, PoolFailure) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow,
                                    absl::string_view(), nullptr);
            return nullptr;
          }));

  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks_, onSuccess_(_, _))
      .WillOnce(Invoke([](const AsyncClient::Request& request, ResponseMessage* response) -> void {
        // The callback gets called before AsyncClient::send() completes, which means that we don't
        // have a request handle to compare to.
        EXPECT_NE(nullptr, &request);
        EXPECT_EQ(503, Utility::getResponseStatus(response->headers()));
      }));

  EXPECT_EQ(nullptr, client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions()));

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, PoolFailureWithBody) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow,
                                    absl::string_view(), nullptr);
            return nullptr;
          }));

  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  EXPECT_CALL(callbacks_, onSuccess_(_, _))
      .WillOnce(Invoke([](const AsyncClient::Request& request, ResponseMessage* response) -> void {
        // The callback gets called before AsyncClient::send() completes, which means that we don't
        // have a request handle to compare to.
        EXPECT_NE(nullptr, &request);
        EXPECT_EQ(503, Utility::getResponseStatus(response->headers()));
      }));
  message_->body().add("hello");
  EXPECT_EQ(nullptr, client_.send(std::move(message_), callbacks_, AsyncClient::RequestOptions()));

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, StreamTimeout) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  TestRequestHeaderMapImpl expected_timeout{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_timeout), false));
  EXPECT_CALL(stream_callbacks_, onData(_, true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(
      stream_callbacks_, AsyncClient::StreamOptions().setTimeout(std::chrono::milliseconds(40)));
  stream->sendHeaders(message_->headers(), true);
  timer_->invokeCallback();

  EXPECT_EQ(1UL,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
}

TEST_F(AsyncClientImplTest, StreamTimeoutHeadReply) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  RequestMessagePtr message{new RequestMessageImpl()};
  message->headers().setMethod("HEAD");
  HttpTestUtility::addDefaultHeaders(message->headers(), false);
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  TestRequestHeaderMapImpl expected_timeout{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_timeout), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  AsyncClient::Stream* stream = client_.start(
      stream_callbacks_, AsyncClient::StreamOptions().setTimeout(std::chrono::milliseconds(40)));
  stream->sendHeaders(message->headers(), true);
  timer_->invokeCallback();
}

TEST_F(AsyncClientImplTest, RequestTimeout) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  auto* request =
      client_.send(std::move(message_), callbacks_,
                   AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(40)));
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 504);
  timer_->invokeCallback();

  EXPECT_EQ(1UL,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
}

TEST_F(AsyncClientImplTracingTest, RequestTimeout) {
  Tracing::MockSpan* child_span{new Tracing::MockSpan()};
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));

  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(parent_span_, spawnChild_(_, "async fake_cluster egress", _))
      .WillOnce(Return(child_span));

  AsyncClient::RequestOptions options = AsyncClient::RequestOptions()
                                            .setParentSpan(parent_span_)
                                            .setTimeout(std::chrono::milliseconds(40));
  EXPECT_CALL(*child_span, setSampled(true));
  EXPECT_CALL(*child_span, injectContext(_, _));

  auto* request = client_.send(std::move(message_), callbacks_, options);
  EXPECT_NE(request, nullptr);

  expectSuccess(request, 504);

  EXPECT_CALL(*child_span, setTag(Eq("onBeforeFinalizeUpstreamSpan"), Eq("called")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().Component), Eq(Tracing::Tags::get().Proxy)));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpProtocol), Eq("HTTP/1.1")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().PeerAddress), Eq("10.0.0.1:443")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().UpstreamCluster), Eq("fake_cluster")));
  EXPECT_CALL(*child_span,
              setTag(Eq(Tracing::Tags::get().UpstreamClusterName), Eq("observability_name")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().HttpStatusCode), Eq("504")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().ResponseFlags), Eq("UT")));
  EXPECT_CALL(*child_span, setTag(Eq(Tracing::Tags::get().Error), Eq(Tracing::Tags::get().True)))
      .Times(AnyNumber());
  EXPECT_CALL(*child_span, finishSpan());
  timer_->invokeCallback();
}

TEST_F(AsyncClientImplTest, DisableTimer) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(200), _));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_,
                   AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(200)));
  EXPECT_CALL(callbacks_, onBeforeFinalizeUpstreamSpan(_, _));
  request->cancel();
}

TEST_F(AsyncClientImplTest, DisableTimerWithStream) {
  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](StreamDecoder&, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            return nullptr;
          }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40), _));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream = client_.start(
      stream_callbacks_, AsyncClient::StreamOptions().setTimeout(std::chrono::milliseconds(40)));
  stream->sendHeaders(message_->headers(), true);
  stream->reset();
}

TEST_F(AsyncClientImplTest, MultipleDataStream) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  Buffer::InstancePtr body2{new Buffer::OwnedImpl("test body2")};

  EXPECT_CALL(cm_.thread_local_cluster_.conn_pool_, newStream(_, _, _))
      .WillOnce(Invoke(
          [&](ResponseDecoder& decoder, ConnectionPool::Callbacks& callbacks,
              const ConnectionPool::Instance::StreamOptions&) -> ConnectionPool::Cancellable* {
            callbacks.onPoolReady(stream_encoder_, cm_.thread_local_cluster_.conn_pool_.host_,
                                  stream_info_, {});
            response_decoder_ = &decoder;
            return nullptr;
          }));

  TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestResponseHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));

  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(
      ResponseHeaderMapPtr(new TestResponseHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body2.get()), true));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body2.get()), true));
  EXPECT_CALL(stream_callbacks_, onComplete());

  stream->sendData(*body2, true);
  response_decoder_->decodeData(*body2, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, WatermarkCallbacks) {
  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers_, false);
  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      dynamic_cast<Http::AsyncStreamImpl*>(stream);
  filter_callbacks->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_TRUE(stream->isAboveWriteBufferHighWatermark());
  filter_callbacks->onDecoderFilterAboveWriteBufferHighWatermark();
  EXPECT_TRUE(stream->isAboveWriteBufferHighWatermark());
  filter_callbacks->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_TRUE(stream->isAboveWriteBufferHighWatermark());
  filter_callbacks->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_FALSE(stream->isAboveWriteBufferHighWatermark());
  EXPECT_CALL(stream_callbacks_, onReset());
}

TEST_F(AsyncClientImplTest, RdsGettersTest) {
  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  stream->sendHeaders(headers_, false);
  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      dynamic_cast<Http::AsyncStreamImpl*>(stream);
  auto route = filter_callbacks->route();
  ASSERT_NE(nullptr, route);
  auto route_entry = route->routeEntry();
  ASSERT_NE(nullptr, route_entry);
  auto& path_match_criterion = route_entry->pathMatchCriterion();
  EXPECT_EQ("", path_match_criterion.matcher());
  EXPECT_EQ(Router::PathMatchType::None, path_match_criterion.matchType());
  const auto& route_config = route_entry->virtualHost().routeConfig();
  EXPECT_EQ("", route_config.name());
  EXPECT_EQ(0, route_config.internalOnlyHeaders().size());
  auto cluster_info = filter_callbacks->clusterInfo();
  ASSERT_NE(nullptr, cluster_info);
  EXPECT_EQ(cm_.thread_local_cluster_.cluster_.info_, cluster_info);
  EXPECT_CALL(stream_callbacks_, onReset());
}

TEST_F(AsyncClientImplTest, DumpState) {
  AsyncClient::Stream* stream = client_.start(stream_callbacks_, AsyncClient::StreamOptions());
  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      dynamic_cast<Http::AsyncStreamImpl*>(stream);

  std::stringstream out;
  filter_callbacks->scope().dumpState(out);
  std::string state = out.str();
  EXPECT_THAT(state, testing::HasSubstr("protocol_: 1"));

  EXPECT_CALL(stream_callbacks_, onReset());
}

} // namespace

// Must not be in anonymous namespace for friend to work.
class AsyncClientImplUnitTest : public AsyncClientImplTest {
public:
  std::unique_ptr<NullRouteImpl> route_impl_{new NullRouteImpl(
      client_.cluster_->name(), client_.singleton_manager_, absl::nullopt,
      Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>(),
      absl::nullopt)};
  NullVirtualHost vhost_;
  NullCommonConfig config_;

  void setupRouteImpl(const std::string& yaml_config) {
    envoy::config::route::v3::RetryPolicy retry_policy;

    TestUtility::loadFromYaml(yaml_config, retry_policy);

    route_impl_ = std::make_unique<NullRouteImpl>(
        client_.cluster_->name(), client_.singleton_manager_, absl::nullopt,
        Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>(),
        std::move(retry_policy));
  }
};

// Test the extended fake route that AsyncClient uses.
TEST_F(AsyncClientImplUnitTest, NullRouteImplInitTest) {
  auto& route_entry = *(route_impl_->routeEntry());

  EXPECT_EQ(nullptr, route_impl_->decorator());
  EXPECT_EQ(nullptr, route_impl_->tracingConfig());
  EXPECT_EQ(Code::InternalServerError, route_entry.clusterNotFoundResponseCode());
  EXPECT_EQ(nullptr, route_entry.corsPolicy());
  EXPECT_EQ(nullptr, route_entry.hashPolicy());
  EXPECT_EQ(1, route_entry.hedgePolicy().initialRequests());
  EXPECT_EQ(0, route_entry.hedgePolicy().additionalRequestChance().numerator());
  EXPECT_FALSE(route_entry.hedgePolicy().hedgeOnPerTryTimeout());
  EXPECT_EQ(nullptr, route_entry.metadataMatchCriteria());
  EXPECT_TRUE(route_entry.rateLimitPolicy().empty());
  EXPECT_TRUE(route_entry.rateLimitPolicy().getApplicableRateLimit(0).empty());
  EXPECT_EQ(absl::nullopt, route_entry.idleTimeout());
  EXPECT_EQ(absl::nullopt, route_entry.grpcTimeoutOffset());
  EXPECT_TRUE(route_entry.opaqueConfig().empty());
  EXPECT_TRUE(route_entry.includeVirtualHostRateLimits());
  EXPECT_EQ(nullptr, route_impl_->typedMetadata().get<Config::TypedMetadata::Object>("bar"));
  EXPECT_TRUE(route_entry.upgradeMap().empty());
  EXPECT_EQ(false, route_entry.internalRedirectPolicy().enabled());
  EXPECT_TRUE(route_entry.shadowPolicies().empty());
  EXPECT_TRUE(route_entry.virtualHost().rateLimitPolicy().empty());
  EXPECT_EQ(nullptr, route_entry.virtualHost().corsPolicy());
  EXPECT_FALSE(route_entry.virtualHost().includeAttemptCountInRequest());
  EXPECT_FALSE(route_entry.virtualHost().includeAttemptCountInResponse());
  EXPECT_FALSE(route_entry.virtualHost().routeConfig().usesVhds());
  EXPECT_EQ(nullptr, route_entry.tlsContextMatchCriteria());
}

TEST_F(AsyncClientImplUnitTest, RouteImplInitTestWithRetryPolicy) {
  const std::string yaml = R"EOF(
per_try_timeout: 30s
num_retries: 10
retry_on: 5xx,gateway-error,connect-failure,reset
retry_back_off:
  base_interval: 0.01s
  max_interval: 30s
)EOF";

  setupRouteImpl(yaml);

  auto& route_entry = *(route_impl_->routeEntry());

  EXPECT_EQ(route_entry.retryPolicy().numRetries(), 10);
  EXPECT_EQ(route_entry.retryPolicy().perTryTimeout(), std::chrono::seconds(30));
  EXPECT_EQ(Router::RetryPolicy::RETRY_ON_CONNECT_FAILURE | Router::RetryPolicy::RETRY_ON_5XX |
                Router::RetryPolicy::RETRY_ON_GATEWAY_ERROR | Router::RetryPolicy::RETRY_ON_RESET,
            route_entry.retryPolicy().retryOn());

  EXPECT_EQ(route_entry.retryPolicy().baseInterval(), std::chrono::milliseconds(10));
  EXPECT_EQ(route_entry.retryPolicy().maxInterval(), std::chrono::seconds(30));
}

TEST_F(AsyncClientImplUnitTest, NullConfig) {
  EXPECT_FALSE(config_.mostSpecificHeaderMutationsWins());
}

TEST_F(AsyncClientImplUnitTest, NullVirtualHost) {
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), vhost_.retryShadowBufferLimit());
}

} // namespace Http
} // namespace Envoy
