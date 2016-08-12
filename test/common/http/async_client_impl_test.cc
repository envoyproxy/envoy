#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"

using testing::_;
using testing::ByRef;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;

namespace Http {

class AsyncClientImplTest : public testing::Test {
public:
  AsyncClientImplTest() { HttpTestUtility::addDefaultHeaders(message_->headers()); }

  MessagePtr message_{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks_;
  ConnectionPool::MockInstance conn_pool_;
  NiceMock<MockStreamEncoder> stream_encoder_;
  StreamDecoder* response_decoder_{};
  NiceMock<Stats::MockStore> stats_store_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
};

TEST_F(AsyncClientImplTest, Basic) {
  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  EXPECT_CALL(callbacks_, onSuccess_(_));

  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_2xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_200"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_2xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_200"));
  EXPECT_CALL(stats_store_, deliverTimingToSinks("cluster.fake_cluster.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.internal.upstream_rq_time", _));

  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, Trailers) {
  message_->body(Buffer::InstancePtr{new Buffer::OwnedImpl("test body")});
  Buffer::Instance& data = *message_->body();

  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             response_decoder_ = &decoder;
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), false));
  EXPECT_CALL(stream_encoder_, encodeData(BufferEqual(&data), true));
  EXPECT_CALL(callbacks_, onSuccess_(_));

  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, false);
  response_decoder_->decodeTrailers(HeaderMapPtr{new HeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, FailRequest) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_503"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_503"));

  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(callbacks_, onFailure(Http::AsyncClient::FailureReason::Reset));

  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
}

TEST_F(AsyncClientImplTest, CancelRequest) {
  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  request->cancel();
}

TEST_F(AsyncClientImplTest, PoolFailure) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_503"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_503"));

  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::ConnectionFailure,
                                conn_pool_.host_);
        return nullptr;
      }));

  EXPECT_CALL(callbacks_, onFailure(Http::AsyncClient::FailureReason::Reset));
  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  EXPECT_EQ(nullptr,
            client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>()));
}

TEST_F(AsyncClientImplTest, RequestTimeout) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_504"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_504"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_timeout"));
  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(callbacks_, onFailure(Http::AsyncClient::FailureReason::RequestTimemout));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, std::chrono::milliseconds(40));
  timer_->callback_();
}

TEST_F(AsyncClientImplTest, DisableTimer) {
  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(200)));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClientImpl client(conn_pool_, "fake_cluster", stats_store_, dispatcher_);
  AsyncClient::RequestPtr request =
      client.send(std::move(message_), callbacks_, std::chrono::milliseconds(200));
  request->cancel();
}

} // Http
