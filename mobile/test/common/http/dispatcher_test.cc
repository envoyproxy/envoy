#include "common/http/async_client_impl.h"
#include "common/http/context_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/http/dispatcher.h"
#include "library/common/http/header_utility.h"
#include "library/common/include/c_types.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Http {

class DispatcherTest : public testing::Test {
public:
  DispatcherTest()
      : http_context_(stats_store_.symbolTable()),
        client_(cm_.thread_local_cluster_.cluster_.info_, stats_store_, event_dispatcher_,
                local_info_, cm_, runtime_, random_,
                Router::ShadowWriterPtr{new NiceMock<Router::MockShadowWriter>()}, http_context_),
        http_dispatcher_(event_dispatcher_, cm_) {
    ON_CALL(*cm_.conn_pool_.host_, locality())
        .WillByDefault(ReturnRef(envoy::api::v2::core::Locality().default_instance()));
  }

  typedef struct {
    bool on_headers;
    bool on_complete;
    bool on_error;
  } callbacks_called;

  Stats::MockIsolatedStatsStore stats_store_;
  MockAsyncClientCallbacks callbacks_;
  MockAsyncClientStreamCallbacks stream_callbacks_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockStreamEncoder> stream_encoder_;
  StreamDecoder* response_decoder_{};
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Event::MockDispatcher> event_dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Http::ContextImpl http_context_;
  AsyncClientImpl client_;
  Dispatcher http_dispatcher_;
  envoy_observer observer_;
};

TEST_F(DispatcherTest, BasicStreamHeadersOnly) {
  // Setup observer to handle the response headers.
  envoy_observer observer;
  callbacks_called cc = {false, false, false};
  observer.context = &cc;
  observer.on_headers = [](envoy_headers c_headers, bool end_stream, void* context) -> void {
    ASSERT_TRUE(end_stream);
    HeaderMapPtr response_headers = Utility::transformHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers = true;
  };
  observer.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  envoy_stream_t stream = http_dispatcher_.startStream(observer);

  // Send request headers.
  Event::PostCb post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, true);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, true));
  post_cb();

  // Decode response headers. decodeHeaders with true will bubble up to onHeaders, which will in
  // turn cause closeRemote. Because closeLocal has already been called, cleanup will happen; hence
  // the second call to isThreadSafe.
  // TODO: find a way to make the fact that the stream is correctly closed more explicit.
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  response_decoder_->decode100ContinueHeaders(
      HeaderMapPtr(new TestHeaderMapImpl{{":status", "100"}}));
  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), true);

  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_200").value());
  EXPECT_EQ(1UL, cm_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("internal.upstream_rq_200")
                     .value());

  // Ensure that the on_headers on the observer was called.
  ASSERT_TRUE(cc.on_headers);
  ASSERT_TRUE(cc.on_complete);
}

TEST_F(DispatcherTest, ResetStream) {
  envoy_observer observer;
  callbacks_called cc = {false, false, false};
  observer.context = &cc;
  observer.on_error = [](envoy_error actual_error, void* context) -> void {
    envoy_error expected_error = {ENVOY_STREAM_RESET, envoy_nodata};
    ASSERT_EQ(actual_error.error_code, expected_error.error_code);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error = true;
  };
  observer.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  envoy_stream_t stream = http_dispatcher_.startStream(observer);

  Event::PostCb post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  http_dispatcher_.resetStream(stream);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  post_cb();

  // Ensure that the on_error on the observer was called.
  ASSERT_TRUE(cc.on_error);
  ASSERT_FALSE(cc.on_complete);
}

TEST_F(DispatcherTest, MultipleStreams) {
  // Start stream1.
  // Setup observer to handle the response headers.
  envoy_observer observer;
  callbacks_called cc = {false, false, false};
  observer.context = &cc;
  observer.on_headers = [](envoy_headers c_headers, bool end_stream, void* context) -> void {
    ASSERT_TRUE(end_stream);
    HeaderMapPtr response_headers = Utility::transformHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers = true;
  };
  observer.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  envoy_stream_t stream = http_dispatcher_.startStream(observer);

  // Send request headers.
  Event::PostCb post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, true);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, true));
  post_cb();

  // Start stream2.
  // Setup observer to handle the response headers.
  NiceMock<MockStreamEncoder> stream_encoder2;
  StreamDecoder* response_decoder2{};
  envoy_observer observer2;
  callbacks_called cc2 = {false, false, false};
  observer2.context = &cc2;
  observer2.on_headers = [](envoy_headers c_headers, bool end_stream, void* context) -> void {
    ASSERT_TRUE(end_stream);
    HeaderMapPtr response_headers = Utility::transformHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "503");
    bool* on_headers_called2 = static_cast<bool*>(context);
    *on_headers_called2 = true;
  };
  observer2.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder2, cm_.conn_pool_.host_);
        response_decoder2 = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers2;
  HttpTestUtility::addDefaultHeaders(headers2);
  envoy_headers c_headers2 = Utility::transformHeaders(headers2);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  EXPECT_CALL(event_dispatcher_, post(_));
  envoy_stream_t stream2 = http_dispatcher_.startStream(observer2);

  // Send request headers.
  Event::PostCb post_cb2;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb2));
  http_dispatcher_.sendHeaders(stream2, c_headers2, true);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder2, encodeHeaders(_, true));
  post_cb2();

  // Finish stream 2.
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  HeaderMapPtr response_headers2(new TestHeaderMapImpl{{":status", "503"}});
  response_decoder2->decodeHeaders(std::move(response_headers2), true);
  // Ensure that the on_headers on the observer was called.
  ASSERT_TRUE(cc2.on_headers);
  ASSERT_TRUE(cc2.on_complete);

  // Finish stream 1.
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  HeaderMapPtr response_headers(new TestHeaderMapImpl{{":status", "200"}});
  response_decoder_->decodeHeaders(std::move(response_headers), true);
  ASSERT_TRUE(cc.on_headers);
  ASSERT_TRUE(cc.on_complete);
}

TEST_F(DispatcherTest, LocalResetAfterStreamStart) {
  envoy_observer observer;
  callbacks_called cc = {false, false, false};
  observer.context = &cc;

  observer.on_error = [](envoy_error actual_error, void* context) -> void {
    envoy_error expected_error = {ENVOY_STREAM_RESET, envoy_nodata};
    ASSERT_EQ(actual_error.error_code, expected_error.error_code);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error = true;
  };
  observer.on_headers = [](envoy_headers c_headers, bool end_stream, void* context) -> void {
    ASSERT_FALSE(end_stream);
    HeaderMapPtr response_headers = Utility::transformHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers = true;
  };
  observer.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  envoy_stream_t stream = http_dispatcher_.startStream(observer);

  // Send request headers.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, false);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
  send_headers_post_cb();

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  // Ensure that the on_headers on the observer was called.
  ASSERT_TRUE(cc.on_headers);

  Event::PostCb reset_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&reset_post_cb));
  http_dispatcher_.resetStream(stream);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  reset_post_cb();

  // Ensure that the on_error on the observer was called.
  ASSERT_TRUE(cc.on_error);
  ASSERT_FALSE(cc.on_complete);
}

TEST_F(DispatcherTest, RemoteResetAfterStreamStart) {
  envoy_observer observer;
  callbacks_called cc = {false, false, false};
  observer.context = &cc;

  observer.on_error = [](envoy_error actual_error, void* context) -> void {
    envoy_error expected_error = {ENVOY_STREAM_RESET, envoy_nodata};
    ASSERT_EQ(actual_error.error_code, expected_error.error_code);
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_error = true;
  };
  observer.on_headers = [](envoy_headers c_headers, bool end_stream, void* context) -> void {
    ASSERT_FALSE(end_stream);
    HeaderMapPtr response_headers = Utility::transformHeaders(c_headers);
    EXPECT_EQ(response_headers->Status()->value().getStringView(), "200");
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_headers = true;
  };
  observer.on_complete = [](void* context) -> void {
    callbacks_called* cc = static_cast<callbacks_called*>(context);
    cc->on_complete = true;
  };

  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(
          WithArg<0>(Invoke([&](AsyncClient::StreamCallbacks& callbacks) -> AsyncClient::Stream* {
            return client_.start(callbacks, AsyncClient::StreamOptions());
          })));
  envoy_stream_t stream = http_dispatcher_.startStream(observer);

  // Send request headers.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, false);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
  send_headers_post_cb();

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  // Ensure that the on_headers on the observer was called.
  ASSERT_TRUE(cc.on_headers);

  stream_encoder_.getStream().resetStream(StreamResetReason::RemoteReset);
  // Ensure that the on_error on the observer was called.
  ASSERT_TRUE(cc.on_error);
  ASSERT_FALSE(cc.on_complete);
}

TEST_F(DispatcherTest, DestroyWithActiveStream) {
  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(Return(client_.start(stream_callbacks_, AsyncClient::StreamOptions())));
  envoy_stream_t stream = http_dispatcher_.startStream(observer_);

  // Send request headers.
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  http_dispatcher_.sendHeaders(stream, c_headers, false);
}

TEST_F(DispatcherTest, ResetInOnHeaders) {
  // Grab the response decoder in order to dispatch responses on the stream.
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder& decoder,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        response_decoder_ = &decoder;
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  // Create a stream.
  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(Return(client_.start(stream_callbacks_, AsyncClient::StreamOptions())));
  envoy_stream_t stream = http_dispatcher_.startStream(observer_);

  // Send request headers.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, false);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, false));
  send_headers_post_cb();

  TestHeaderMapImpl expected_headers{{":status", "200"}};
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(event_dispatcher_, post(_));
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_headers), false))
      .WillOnce(Invoke([this, stream](HeaderMap&, bool) { http_dispatcher_.resetStream(stream); }));
  EXPECT_CALL(stream_callbacks_, onData(_, _)).Times(0);
  EXPECT_CALL(stream_callbacks_, onReset());

  response_decoder_->decodeHeaders(HeaderMapPtr(new TestHeaderMapImpl{{":status", "200"}}), false);
  // TODO: Need to finish the data side (sendData, onData) in order to fully verify that onData was
  // never called due to the reset.
}

TEST_F(DispatcherTest, StreamTimeout) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  envoy_headers c_headers = Utility::transformHeaders(headers);

  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(Return(client_.start(stream_callbacks_, AsyncClient::StreamOptions().setTimeout(
                                                            std::chrono::milliseconds(40)))));
  envoy_stream_t stream = http_dispatcher_.startStream(observer_);

  // Send request headers.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, true);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, true));
  timer_ = new NiceMock<Event::MockTimer>(&event_dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  TestHeaderMapImpl expected_timeout{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_timeout), false));
  EXPECT_CALL(stream_callbacks_, onData(_, true));
  EXPECT_CALL(stream_callbacks_, onComplete());
  send_headers_post_cb();
  timer_->callback_();

  EXPECT_EQ(1UL,
            cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_timeout")
                .value());
  EXPECT_EQ(1UL, cm_.conn_pool_.host_->stats().rq_timeout_.value());
  EXPECT_EQ(
      1UL,
      cm_.thread_local_cluster_.cluster_.info_->stats_store_.counter("upstream_rq_504").value());
}

TEST_F(DispatcherTest, StreamTimeoutHeadReply) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  // Build a set of request headers.
  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers, "HEAD");
  envoy_headers c_headers = Utility::transformHeaders(headers);

  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(Return(client_.start(stream_callbacks_, AsyncClient::StreamOptions().setTimeout(
                                                            std::chrono::milliseconds(40)))));
  envoy_stream_t stream = http_dispatcher_.startStream(observer_);

  // Send request headers.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, true);

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  EXPECT_CALL(stream_encoder_, encodeHeaders(_, true));
  timer_ = new NiceMock<Event::MockTimer>(&event_dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));

  TestHeaderMapImpl expected_timeout{
      {":status", "504"}, {"content-length", "24"}, {"content-type", "text/plain"}};
  EXPECT_CALL(stream_callbacks_, onHeaders_(HeaderMapEqualRef(&expected_timeout), true));
  EXPECT_CALL(stream_callbacks_, onComplete());
  send_headers_post_cb();
  timer_->callback_();
}

TEST_F(DispatcherTest, DisableTimerWithStream) {
  EXPECT_CALL(cm_.conn_pool_, newStream(_, _))
      .WillOnce(Invoke([&](StreamDecoder&,
                           ConnectionPool::Callbacks& callbacks) -> ConnectionPool::Cancellable* {
        callbacks.onPoolReady(stream_encoder_, cm_.conn_pool_.host_);
        return nullptr;
      }));

  TestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers, "HEAD");
  envoy_headers c_headers = Utility::transformHeaders(headers);

  EXPECT_CALL(cm_, httpAsyncClientForCluster("egress_cluster"))
      .WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(cm_.async_client_, start(_, _))
      .WillOnce(Return(client_.start(stream_callbacks_, AsyncClient::StreamOptions().setTimeout(
                                                            std::chrono::milliseconds(40)))));
  envoy_stream_t stream = http_dispatcher_.startStream(observer_);

  // Send request headers and reset stream.
  Event::PostCb send_headers_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&send_headers_post_cb));
  http_dispatcher_.sendHeaders(stream, c_headers, true);
  Event::PostCb reset_stream_post_cb;
  EXPECT_CALL(event_dispatcher_, post(_)).WillOnce(SaveArg<0>(&reset_stream_post_cb));
  http_dispatcher_.resetStream(stream);

  EXPECT_CALL(stream_encoder_, encodeHeaders(_, true));
  timer_ = new NiceMock<Event::MockTimer>(&event_dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(*timer_, disableTimer());
  EXPECT_CALL(stream_encoder_.stream_, resetStream(_));
  EXPECT_CALL(stream_callbacks_, onReset());

  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  send_headers_post_cb();
  EXPECT_CALL(event_dispatcher_, isThreadSafe()).Times(1).WillRepeatedly(Return(true));
  reset_stream_post_cb();
}

} // namespace Http
} // namespace Envoy
