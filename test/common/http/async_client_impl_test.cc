#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
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

class AsyncClientImplTest : public testing::Test, public AsyncClientConnPoolFactory {
public:
  AsyncClientImplTest() {
    HttpTestUtility::addDefaultHeaders(message_->headers());
    ON_CALL(*conn_pool_.host_, zone()).WillByDefault(ReturnRef(upstream_zone_));
  }

  // Http::AsyncClientConnPoolFactory
  Http::ConnectionPool::Instance* connPool() override { return &conn_pool_; }

  std::string upstream_zone_{"to_az"};
  MessagePtr message_{new RequestMessageImpl()};
  MockAsyncClientCallbacks callbacks_;
  ConnectionPool::MockInstance conn_pool_;
  NiceMock<MockStreamEncoder> stream_encoder_;
  StreamDecoder* response_decoder_{};
  NiceMock<Stats::MockStore> stats_store_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockCluster> cluster_;
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_200"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_2xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_200"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_2xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_2xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_200"));
  EXPECT_CALL(stats_store_, deliverTimingToSinks("cluster.fake_cluster.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.internal.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_time", _));

  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, MultipleRequests) {
  // Send request 1
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  // Send request 2.
  MessagePtr message2{new RequestMessageImpl()};
  HttpTestUtility::addDefaultHeaders(message2->headers());
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};
  MockAsyncClientCallbacks callbacks2;
  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder2, conn_pool_.host_);
                             response_decoder2 = &decoder;
                             return nullptr;
                           }));
  EXPECT_CALL(stream_encoder2, encodeHeaders(HeaderMapEqualRef(ByRef(message2->headers())), true));
  client.send(std::move(message2), callbacks2, Optional<std::chrono::milliseconds>());

  // Finish request 2.
  HeaderMapPtr response_headers2(new HeaderMapImpl{{":status", "503"}});
  EXPECT_CALL(callbacks2, onSuccess_(_));
  response_decoder2->decodeHeaders(std::move(response_headers2), true);

  // Finish request 1.
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  EXPECT_CALL(callbacks_, onSuccess_(_));
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  HeaderMapPtr response_headers(new HeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  response_decoder_->decodeData(data, false);
  response_decoder_->decodeTrailers(HeaderMapPtr{new HeaderMapImpl{{"some", "trailer"}}});
}

TEST_F(AsyncClientImplTest, FailRequest) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_503"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_503"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_5xx"));
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  AsyncClient::Request* request =
      client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());
  request->cancel();
}

TEST_F(AsyncClientImplTest, PoolFailure) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_503"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_503"));
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
  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  EXPECT_EQ(nullptr,
            client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>()));
}

TEST_F(AsyncClientImplTest, RequestTimeout) {
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.upstream_rq_504"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_504"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_5xx"));
  EXPECT_CALL(stats_store_, counter("cluster.fake_cluster.internal.upstream_rq_504"));
  EXPECT_CALL(conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&, ConnectionPool::Callbacks& callbacks)
                           -> ConnectionPool::Cancellable* {
                             callbacks.onPoolReady(stream_encoder_, conn_pool_.host_);
                             return nullptr;
                           }));

  EXPECT_CALL(stream_encoder_, encodeHeaders(HeaderMapEqualRef(ByRef(message_->headers())), true));
  EXPECT_CALL(callbacks_, onFailure(Http::AsyncClient::FailureReason::RequestTimeout));
  timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, std::chrono::milliseconds(40));
  timer_->callback_();

  EXPECT_EQ(1UL, cluster_.stats_store_.counter("cluster.fake_cluster.upstream_rq_timeout").value());
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
  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  AsyncClient::Request* request =
      client.send(std::move(message_), callbacks_, std::chrono::milliseconds(200));
  request->cancel();
}

TEST_F(AsyncClientImplTest, CanaryStatusTrue) {
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  HeaderMapPtr response_headers(
      new HeaderMapImpl{{":status", "200"}, {"x-envoy-upstream-canary", "false"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  EXPECT_CALL(*conn_pool_.host_, canary()).WillOnce(Return(true));
  EXPECT_CALL(stats_store_, deliverTimingToSinks("cluster.fake_cluster.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.internal.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_time", _));

  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.canary.upstream_rq_time", _));
  response_decoder_->decodeData(data, true);
}

TEST_F(AsyncClientImplTest, CanaryStatusFalse) {
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

  AsyncClientImpl client(cluster_, *this, stats_store_, dispatcher_, "from_az");
  client.send(std::move(message_), callbacks_, Optional<std::chrono::milliseconds>());

  HeaderMapPtr response_headers(
      new HeaderMapImpl{{":status", "200"}, {"x-envoy-upstream-canary", "false"}});
  response_decoder_->decodeHeaders(std::move(response_headers), false);
  EXPECT_CALL(*conn_pool_.host_, canary()).WillOnce(Return(false));
  EXPECT_CALL(stats_store_, deliverTimingToSinks("cluster.fake_cluster.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.internal.upstream_rq_time", _));
  EXPECT_CALL(stats_store_,
              deliverTimingToSinks("cluster.fake_cluster.zone.from_az.to_az.upstream_rq_time", _));
  response_decoder_->decodeData(data, true);
}

} // Http
