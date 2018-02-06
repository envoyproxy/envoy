#include <chrono>
#include <cstdint>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Http {
namespace {

class AsyncClientImplTest : public testing::Test {
public:
  AsyncClientImplTest()
      : client_(*cm_.thread_local_cluster_.cluster_.info_, stats_store_, dispatcher_, local_info_,
                cm_, runtime_, random_,
                Router::ShadowWriterPtr{new NiceMock<Router::MockShadowWriter>()}) {
    message_->headers().insertMethod().value(std::string("GET"));
    message_->headers().insertHost().value(std::string("host"));
    message_->headers().insertPath().value(std::string("/"));
    ON_CALL(*cm_.conn_pool_.host_, locality())
        .WillByDefault(ReturnRef(envoy::api::v2::core::Locality().default_instance()));
  }

  void expectSuccess(uint64_t code) {
    EXPECT_CALL(callbacks_, onSuccess_(_)).WillOnce(Invoke([code](Message* response) -> void {
      EXPECT_EQ(code, Utility::getResponseStatus(response->headers()));
    }));
  }

  void expectResponseHeaders(MockAsyncClientStreamCallbacks& callbacks, uint64_t code,
                             bool end_stream) {
    EXPECT_CALL(callbacks, onHeaders_(_, end_stream))
        .WillOnce(Invoke([code](HeaderMap& headers, bool) -> void {
          EXPECT_EQ(std::to_string(code), headers.Status()->value().c_str());
        }));
  }

  MessagePtr message_{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks_;
  MockAsyncClientStreamCallbacks stream_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockStreamEncoder> stream_encoder_;
  StreamDecoder* response_decoder_{};
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  AsyncClientImpl client_;
};

TEST_F(AsyncClientImplTest, BasicStream) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  response_decoder_->decode100ContinueHeaders(
      HeaderMapPtr(new TestHeaderMapImpl{{":status", "100"}}));
  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, Basic) {
  message_->body().reset(new Buffer::OwnedImpl("test body"));
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl copy(message_->headers());
  copy.addCopy("x-envoy-internal", "true");
  copy.addCopy("x-forwarded-for", "127.0.0.1");
  copy.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&copy), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  expectSuccess(200);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());
}

TEST_F(AsyncClientImplTest, Retry) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));
  Message* message_copy = message_.get();

  message_->body().reset(new Buffer::OwnedImpl("test body"));
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  message_->headers().insertEnvoyRetryOn().value(Headers::get().EnvoyRetryOnValues._5xx);
  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_copy->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  timer_->callback_();

  // Normal response.
  expectSuccess(200);
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
}

TEST_F(AsyncClientImplTest, RetryWithStream) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  headers.insertEnvoyRetryOn().value(Headers::get().EnvoyRetryOnValues._5xx);
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), true);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));
  timer_->callback_();

  // Normal response.
  expectResponseHeaders(stream_callbacks_, 200, true);
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
}

TEST_F(AsyncClientImplTest, MultipleStreams) {
  // Start stream 1
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers(message_->headers());
  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Start stream 2
  Buffer::InstancePtr body2{new Buffer::OwnedImpl("test body")};
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};
  MockAsyncClientStreamCallbacks stream_callbacks2;

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder2, cm_.conn_pool_.host_);
        response_decoder2 = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers2(message_->headers());
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&headers2), false));
  EXPECT_CALL(stream_encoder2, encodeData(BufferEqual(body2.get()), true));

  expectResponseHeaders(stream_callbacks2, 503, true);

  AsyncClient::Stream* stream2 =
      client_.start(stream_callbacks2, Optional<std::chrono::milliseconds>(), false);
  stream2->sendHeaders(headers2, false);
  stream2->sendData(*body2, true);

  // Finish stream 2.
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "503"}});
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish stream 1.
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(*body, true);
}

TEST_F(AsyncClientImplTest, MultipleRequests) {
  // Send request 1
  message_->body().reset(new Buffer::OwnedImpl("test body"));
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Send request 2.
  MessagePtr message2{new RequestMessageImpl()};
  HttpTestUtility::addDefaultHeaders(message2->headers());
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};
  MockAsyncClientCallbacks callbacks2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder2, cm_.conn_pool_.host_);
        response_decoder2 = &decoder;
        return nullptr;
      }));
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&message2->headers()), true));
  client_.send(std::move(message2), callbacks2, Optional<std::chrono::milliseconds>());

  // Finish request 2.
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(callbacks2, onSuccess_(_));
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish request 1.
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  expectSuccess(200);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, StreamAndRequest) {
  // Send request
  message_->body().reset(new Buffer::OwnedImpl("test body"));
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Start stream
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder2, cm_.conn_pool_.host_);
        response_decoder2 = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder2, encodeData(BufferEqual(body.get()), true));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), true));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, true);

  // Finish stream.
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder2->decodeHeaders(std::move(response_headers2), false);
  response_decoder2->decodeData(*body, true);

  // Finish request.
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  expectSuccess(200);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, StreamWithTrailers) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  TestHeaderMapImpl trailers{{"some", "request_trailer"}};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_encoder_, encodeTrailers(HeaderMapEqualRef(&trailers)));

  expectResponseHeaders(stream_callbacks_, 200, false);
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  TestHeaderMapImpl expected_trailers{{"some", "trailer"}};
  EXPECT_CALL(stream_callbacks_, onTrailers_(HeaderMapEqualRef(&expected_trailers)));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);
  stream->sendTrailers(trailers);

  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(*body, false);
  response_decoder_->decodeTrailers(HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, Trailers) {
  message_->body().reset(new Buffer::OwnedImpl("test body"));
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  expectSuccess(200);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, false);
  response_decoder_->decodeTrailers(HeaderMapPtr{new TestHeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, ImmediateReset) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  expectSuccess(503);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, LocalResetAfterStreamStart) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  stream->reset();
}

// Validate behavior when the stream's onHeaders() callback performs a stream
// reset.
TEST_F(AsyncClientImplTest, ResetInOnHeaders) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);

  TestHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false))
      .WillOnce(Invoke([&stream](HeaderMap&, bool) { stream->reset(); }));
  EXPECT_CALL(stream_callbacks_, onData(_, _)).Times(0);
  EXPECT_CALL(stream_callbacks_, onReset());

  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      static_cast<Http::AsyncStreamImpl*>(stream);
  filter_callbacks->encodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
}

TEST_F(AsyncClientImplTest, RemoteResetAfterStreamStart) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTest, ResetAfterResponseStart) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(callbacks_, onFailure(_));

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTest, ResetStream) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(message_->headers(), true);
  stream->reset();
}

TEST_F(AsyncClientImplTest, CancelRequest) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  request->cancel();
}

TEST_F(AsyncClientImplTest, DestroyWithActiveStream) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), false));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(message_->headers(), false);
}

TEST_F(AsyncClientImplTest, DestroyWithActiveRequest) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(callbacks_, onFailure(_));
  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
}

TEST_F(AsyncClientImplTest, PoolFailure) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
        return nullptr;
      }));

  expectSuccess(503);
  EXPECT_EQ(nullptr,
            client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>()));

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, PoolFailureWithBody) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
        return nullptr;
      }));

  expectSuccess(503);
  message_->body().reset(new Buffer::OwnedImpl("hello"));
  EXPECT_EQ(nullptr,
            client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>()));

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, StreamTimeout) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  TestHeaderMapImpl expected_timeout{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_timeout), false));
  EXPECT_CALL(stream_callbacks_, onData(_, true));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, std::chrono::milliseconds(40), false);
  stream->sendHeaders(message_->headers(), true);
  timer_->callback_();

  EXPECT_EQ(1UL,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
}

TEST_F(AsyncClientImplTest, RequestTimeout) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  expectSuccess(504);
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  client_.send(std::move(message_), callbacks_, std::chrono::milliseconds(40));
  timer_->callback_();

  EXPECT_EQ(1UL,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
}

TEST_F(AsyncClientImplTest, DisableTimer) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(200)));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_, std::chrono::milliseconds(200));
  request->cancel();
}

TEST_F(AsyncClientImplTest, DisableTimerWithStream) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&message_->headers()), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, std::chrono::milliseconds(40), false);
  stream->sendHeaders(message_->headers(), true);
  stream->reset();
}

TEST_F(AsyncClientImplTest, MultipleDataStream) {
  Buffer::InstancePtr body{new Buffer::OwnedImpl("test body")};
  Buffer::InstancePtr body2{new Buffer::OwnedImpl("test body2")};

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.addCopy("x-envoy-internal", "true");
  headers.addCopy("x-forwarded-for", "127.0.0.1");
  headers.addCopy(":scheme", "http");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(&headers), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body.get()), false));

  TestHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body.get()), false));

  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  stream->sendData(*body, false);

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  response_decoder_->decodeData(*body, false);

  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(body2.get()), true));
  EXPECT_CALL(stream_callbacks_, onData(BufferEqual(body2.get()), true));

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
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      static_cast<Http::AsyncStreamImpl*>(stream);
  filter_callbacks->onDecoderFilterAboveWriteBufferHighWatermark();
  filter_callbacks->onDecoderFilterBelowWriteBufferLowWatermark();
  EXPECT_CALL(stream_callbacks_, onReset());
}

TEST_F(AsyncClientImplTest, RdsGettersTest) {
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  AsyncClient::Stream* stream =
      client_.start(stream_callbacks_, Optional<std::chrono::milliseconds>(), false);
  stream->sendHeaders(headers, false);
  Http::StreamDecoderFilterCallbacks* filter_callbacks =
      static_cast<Http::AsyncStreamImpl*>(stream);
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
  EXPECT_EQ(nullptr, route_config.route(headers, 0));
  EXPECT_CALL(stream_callbacks_, onReset());
}

} // namespace
} // namespace Http
} // namespace Envoy
