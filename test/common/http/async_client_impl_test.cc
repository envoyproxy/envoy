#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::ByRef;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Http {

class AsyncClientImplTest : public testing::Test {
public:
  AsyncClientImplTest()
      : client_(cm_.cluster_, stats_store_, dispatcher_, "from_az", cm_, runtime_, random_,
                Router::ShadowWriterPtr{new NiceMock<Router::MockShadowWriter>()},
                "local_address") {
    HttpTestUtility::addDefaultHeaders(message_->headers());
    ON_CALL(*cm_.conn_pool_.host_, zone()).WillByDefault(ReturnRef(upstream_zone_));
    ON_CALL(cm_.cluster_, altStatName()).WillByDefault(ReturnRef(EMPTY_STRING));
  }

  void expectSuccess(uint64_t code) {
    EXPECT_CALL(callbacks_, onSuccess_(_))
        .WillOnce(Invoke([code](Message* response) -> void {
          EXPECT_EQ(code, Utility::getResponseStatus(response->headers()));
        }));
  }

  std::string upstream_zone_{"to_az"};
  MessagePtr message_{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockStreamEncoder> stream_encoder_;
  StreamDecoder* response_decoder_{};
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Stats::IsolatedStoreImpl stats_store_;
  AsyncClientImpl client_;
};

TEST_F(AsyncClientImplTest, Basic) {
  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  HeaderMapImpl copy(message_->headers());
  copy.addViaCopy("x-envoy-internal", "true");
  copy.addViaCopy("x-forwarded-for", "local_address");

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(copy)), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  expectSuccess(200);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);

  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.upstream_rq_200").value());
  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.internal.upstream_rq_200").value());
}

TEST_F(AsyncClientImplTest, Retry) {
  ON_CALL(runtime_.snapshot_, featureEnabled("upstream.use_retry", 100))
      .WillByDefault(Return(true));
  Message* message_copy = message_.get();

  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  message_->headers().addViaCopy(Headers::get().EnvoyRetryOn,
                                 Headers::get().EnvoyRetryOnValues._5xx);
  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Expect retry and retry timer create.
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "503"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);

  // Retry request.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_,
              encodeHeaders(HeaderMapEqualRef(ByRef(message_copy->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  timer_->callback_();

  // Normal response.
  expectSuccess(200);
  HeaderMapPtr response_headers2(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers2), true);
}

TEST_F(AsyncClientImplTest, MultipleRequests) {
  // Send request 1
  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Send request 2.
  MessagePtr message2{new RequestMessageImpl()};
  HttpTestUtility::addDefaultHeaders(message2->headers());
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};
  MockAsyncClientCallbacks callbacks2;
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder2, cm_.conn_pool_.host_);
                             response_decoder2 = &decoder;
                             return nullptr;
                           }));
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(ByRef(message2->headers())), true));
  client_.send(std::move(message2), callbacks2, Optional<std::chrono::milliseconds>());

  // Finish request 2.
  HeaderMapPtr response_headers2(new HeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(callbacks2, onSuccess_(_));
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish request 1.
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  expectSuccess(200);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, Trailers) {
  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  expectSuccess(200);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, false);
  response_decoder_->decodeTrailers(HeaderMapPtr{new HeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, ImmediateReset) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  expectSuccess(503);

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);

  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, ResetAfterResponseStart) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(callbacks_, onFailure(_));

  client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTest, CancelRequest) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  request->cancel();
}

TEST_F(AsyncClientImplTest, PoolFailure) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::ConnectionFailure,
                                cm_.conn_pool_.host_);
        return nullptr;
      }));

  expectSuccess(503);
  EXPECT_EQ(nullptr,
            client_.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>()));

  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.upstream_rq_503").value());
}

TEST_F(AsyncClientImplTest, RequestTimeout) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  expectSuccess(504);
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  client_.send(std::move(message_), callbacks_, std::chrono::milliseconds(40));
  timer_->callback_();

  EXPECT_EQ(1UL,
            cm_.cluster_.stats_store_.counter("cluster.fake_cluster.upstream_rq_timeout").value());
  EXPECT_EQ(1UL, stats_store_.counter("cluster.fake_cluster.upstream_rq_504").value());
}

TEST_F(AsyncClientImplTest, DisableTimer) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(200)));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClient::Request* request =
      client_.send(std::move(message_), callbacks_, std::chrono::milliseconds(200));
  request->cancel();
}

} // Http
