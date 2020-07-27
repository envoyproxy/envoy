#include <cstdint>
#include <memory>
#include <vector>

#include "common/event/dispatcher_impl.h"
#include "common/http/http2/conn_pool.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/transport_socket_match.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
namespace Http2 {

class TestConnPoolImpl : public ConnPoolImpl {
public:
  using ConnPoolImpl::ConnPoolImpl;

  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override {
    // We expect to own the connection, but already have it, so just release it to prevent it from
    // getting deleted.
    data.connection_.release();
    return CodecClientPtr{createCodecClient_(data)};
  }

  MOCK_METHOD(CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData & data));
};

class ActiveTestRequest;

class Http2ConnPoolImplTest : public testing::Test {
public:
  struct TestCodecClient {
    Http::MockClientConnection* codec_;
    Network::MockClientConnection* connection_;
    CodecClientForTest* codec_client_;
    Event::MockTimer* connect_timer_;
    Event::DispatcherPtr client_dispatcher_;
  };

  Http2ConnPoolImplTest()
      : api_(Api::createApiForTest(stats_store_)),
        pool_(std::make_unique<TestConnPoolImpl>(
            dispatcher_, host_, Upstream::ResourcePriority::Default, nullptr, nullptr)) {
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
  }

  ~Http2ConnPoolImplTest() override {
    EXPECT_EQ("", TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges()));
  }

  TestCodecClient& createTestClient() {
    test_clients_.emplace_back();
    TestCodecClient& test_client = test_clients_.back();
    test_client.connection_ = new NiceMock<Network::MockClientConnection>();
    test_client.codec_ = new NiceMock<Http::MockClientConnection>();
    test_client.connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    test_client.client_dispatcher_ = api_->allocateDispatcher("test_thread");
    EXPECT_CALL(*test_client.connect_timer_, enableTimer(_, _));

    return test_client;
  }

  void expectConnectionSetupForClient(TestCodecClient& test_client,
                                      absl::optional<uint32_t> buffer_limits = {}) {
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(test_client.connection_));
    auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    Network::ClientConnectionPtr connection{test_client.connection_};
    test_client.codec_client_ = new CodecClientForTest(
        CodecClient::Type::HTTP1, std::move(connection), test_client.codec_,
        [this](CodecClient*) -> void { onClientDestroy(); },
        Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000"), *test_client.client_dispatcher_);
    if (buffer_limits) {
      EXPECT_CALL(*cluster_, perConnectionBufferLimitBytes()).WillOnce(Return(*buffer_limits));
      EXPECT_CALL(*test_clients_.back().connection_, setBufferLimits(*buffer_limits));
    }
    EXPECT_CALL(*pool_, createCodecClient_(_))
        .WillOnce(Invoke([this](Upstream::Host::CreateConnectionData&) -> CodecClient* {
          return test_clients_.back().codec_client_;
        }));
  }

  // Creates a new test client, expecting a new connection to be created and associated
  // with the new client.
  void expectClientCreate(absl::optional<uint32_t> buffer_limits = {}) {
    expectConnectionSetupForClient(createTestClient(), buffer_limits);
  }

  // Connects a pending connection for client with the given index, asserting
  // that the provided request receives onPoolReady.
  void expectClientConnect(size_t index, ActiveTestRequest& r);
  // Asserts that onPoolReady is called on the request.
  void expectStreamConnect(size_t index, ActiveTestRequest& r);

  // Resets the connection belonging to the provided index, asserting that the
  // provided request receives onPoolFailure.
  void expectClientReset(size_t index, ActiveTestRequest& r, bool local_failure);
  // Asserts that the provided requests receives onPoolFailure.
  void expectStreamReset(ActiveTestRequest& r);

  /**
   * Closes a test client.
   */
  void closeClient(size_t index);

  /**
   * Completes an active request. Useful when this flow is not part of the main test assertions.
   */
  void completeRequest(ActiveTestRequest& r);

  /**
   * Completes an active request and closes the upstream connection. Useful when this flow is
   * not part of the main test assertions.
   */
  void completeRequestCloseUpstream(size_t index, ActiveTestRequest& r);

  MOCK_METHOD(void, onClientDestroy, ());

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80")};
  std::unique_ptr<TestConnPoolImpl> pool_;
  std::vector<TestCodecClient> test_clients_;
  NiceMock<Runtime::MockLoader> runtime_;
};

class ActiveTestRequest {
public:
  ActiveTestRequest(Http2ConnPoolImplTest& test, size_t client_index, bool expect_connected) {
    if (expect_connected) {
      EXPECT_CALL(*test.test_clients_[client_index].codec_, newStream(_))
          .WillOnce(DoAll(SaveArgAddress(&inner_decoder_), ReturnRef(inner_encoder_)));
      EXPECT_CALL(callbacks_.pool_ready_, ready());
      EXPECT_EQ(nullptr, test.pool_->newStream(decoder_, callbacks_));
    } else {
      handle_ = test.pool_->newStream(decoder_, callbacks_);
      EXPECT_NE(nullptr, handle_);
    }
  }

  MockResponseDecoder decoder_;
  ConnPoolCallbacks callbacks_;
  ResponseDecoder* inner_decoder_{};
  NiceMock<MockRequestEncoder> inner_encoder_;
  ConnectionPool::Cancellable* handle_{};
};

void Http2ConnPoolImplTest::expectClientConnect(size_t index, ActiveTestRequest& r) {
  expectStreamConnect(index, r);
  EXPECT_CALL(*test_clients_[index].connect_timer_, disableTimer());
  test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::Connected);
}

void Http2ConnPoolImplTest::expectStreamConnect(size_t index, ActiveTestRequest& r) {
  EXPECT_CALL(*test_clients_[index].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r.inner_decoder_), ReturnRef(r.inner_encoder_)));
  EXPECT_CALL(r.callbacks_.pool_ready_, ready());
}

void Http2ConnPoolImplTest::expectClientReset(size_t index, ActiveTestRequest& r,
                                              bool local_failure) {
  expectStreamReset(r);
  EXPECT_CALL(*test_clients_[0].connect_timer_, disableTimer());
  if (local_failure) {
    test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(r.callbacks_.reason_, ConnectionPool::PoolFailureReason::LocalConnectionFailure);
  } else {
    test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
    EXPECT_EQ(r.callbacks_.reason_, ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  }
}

void Http2ConnPoolImplTest::expectStreamReset(ActiveTestRequest& r) {
  EXPECT_CALL(r.callbacks_.pool_failure_, ready());
}

void Http2ConnPoolImplTest::closeClient(size_t index) {
  test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

void Http2ConnPoolImplTest::completeRequest(ActiveTestRequest& r) {
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  r.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r.decoder_, decodeHeaders_(_, true));
  r.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
}

void Http2ConnPoolImplTest::completeRequestCloseUpstream(size_t index, ActiveTestRequest& r) {
  completeRequest(r);
  closeClient(index);
}

/**
 * Verify that the pool retains and returns the host it was constructed with.
 */
TEST_F(Http2ConnPoolImplTest, Host) { EXPECT_EQ(host_, pool_->host()); }

/**
 * Verify that idle connections are closed immediately when draining.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnectionIdle) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);
  expectClientConnect(0, r);
  completeRequest(r);

  EXPECT_CALL(*this, onClientDestroy());
  pool_->drainConnections();
}

/**
 * Verify that we set the ALPN fallback.
 */
TEST_F(Http2ConnPoolImplTest, VerifyAlpnFallback) {
  InSequence s;

  // Override the TransportSocketFactory with a mock version we can add expectations to.
  auto factory = std::make_unique<Network::MockTransportSocketFactory>();
  auto factory_ptr = factory.get();
  cluster_->transport_socket_matcher_ =
      std::make_unique<NiceMock<Upstream::MockTransportSocketMatcher>>(std::move(factory));

  // Recreate the conn pool so that the host re-evaluates the transport socket match, arriving at
  // our test transport socket factory.
  host_ = Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80");
  pool_ = std::make_unique<TestConnPoolImpl>(dispatcher_, host_,
                                             Upstream::ResourcePriority::Default, nullptr, nullptr);

  // This requires some careful set up of expectations ordering: the call to createTransportSocket
  // happens before all the connection set up but after the test client is created (due to some)
  // of the mocks that are constructed as part of the test client.
  auto& client = createTestClient();
  EXPECT_CALL(*factory_ptr, createTransportSocket(_))
      .WillOnce(Invoke(
          [](Network::TransportSocketOptionsSharedPtr options) -> Network::TransportSocketPtr {
            EXPECT_TRUE(options != nullptr);
            EXPECT_EQ(options->applicationProtocolFallback(),
                      Http::Utility::AlpnNames::get().Http2);
            return std::make_unique<Network::RawBufferSocket>();
          }));
  expectConnectionSetupForClient(client);
  ActiveTestRequest r(*this, 0, false);
  expectClientConnect(0, r);
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  r.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r.decoder_, decodeHeaders_(_, true));
  EXPECT_CALL(*this, onClientDestroy());
  r.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  // Close connections.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Verify that a ready connection with a request in progress is moved to
 * draining and closes when the request completes.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnectionReadyWithRequest) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);
  expectClientConnect(0, r);
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  r.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  pool_->drainConnections();

  EXPECT_CALL(r.decoder_, decodeHeaders_(_, true));
  EXPECT_CALL(*this, onClientDestroy());
  r.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
}

/**
 * Verify that a busy connection is moved to draining and closes when all requests
 * complete.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnectionBusy) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);
  expectClientConnect(0, r);
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  r.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  pool_->drainConnections();

  EXPECT_CALL(r.decoder_, decodeHeaders_(_, true));
  EXPECT_CALL(*this, onClientDestroy());
  r.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
}

/**
 * Verify that draining connections with a pending request does not
 * close the connection, but draining without a pending request does close
 * the connection.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnectionConnecting) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);

  // Pending request prevents the connection from being drained
  pool_->drainConnections();

  // Cancel the pending request, and then the connection can be closed.
  r.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::Default);
  EXPECT_CALL(*this, onClientDestroy());
  pool_->drainConnections();
}

/**
 * Verify that on CloseExcess, the connection is destroyed immediately.
 */
TEST_F(Http2ConnPoolImplTest, CloseExcess) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);

  // Pending request prevents the connection from being drained
  pool_->drainConnections();

  EXPECT_CALL(*this, onClientDestroy());
  r.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
}

/**
 * Verify that on CloseExcess connections are destroyed when they can be.
 */
TEST_F(Http2ConnPoolImplTest, CloseExcessTwo) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);

  expectClientCreate();
  ActiveTestRequest r2(*this, 0, false);
  {
    EXPECT_CALL(*this, onClientDestroy());
    r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }

  {
    EXPECT_CALL(*this, onClientDestroy());
    r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
}

/**
 * Verify that on CloseExcess, the connections are destroyed iff they are actually excess.
 */
TEST_F(Http2ConnPoolImplTest, CloseExcessMultipleRequests) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(3);
  InSequence s;

  // With 3 requests per connection, the first request will result in a client
  // connection, and the next two will be queued for that connection.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // The fourth request will kick off a second connection, and the fifth will plan to share it.
  expectClientCreate();
  ActiveTestRequest r4(*this, 0, false);
  ActiveTestRequest r5(*this, 0, false);

  // The section below cancels the active requests in fairly random order, to
  // ensure there's no association between the requests and the clients created
  // for them.

  // The first cancel will not destroy any clients, as there are still four pending
  // requests and they can not all share the first connection.
  {
    EXPECT_CALL(*this, onClientDestroy()).Times(0);
    r5.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
  // The second cancel will destroy one client, as there will be three pending requests
  // remaining, and they only need one connection.
  {
    EXPECT_CALL(*this, onClientDestroy());
    r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }

  // The next two calls will not destroy the final client, as there are two other
  // pending requests waiting on it.
  {
    EXPECT_CALL(*this, onClientDestroy()).Times(0);
    r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
    r4.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
  // Finally with the last request gone, the final client is destroyed.
  {
    EXPECT_CALL(*this, onClientDestroy());
    r3.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
}

TEST_F(Http2ConnPoolImplTest, CloseExcessMixedMultiplexing) {
  InSequence s;

  // Create clients with in-order capacity:
  // 3  2  6
  // Connection capacity is min(max requests per connection, max concurrent streams).
  // Use maxRequestsPerConnection here since max requests is tested above.
  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(3));
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(2));
  expectClientCreate();
  ActiveTestRequest r4(*this, 0, false);
  ActiveTestRequest r5(*this, 0, false);

  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(6));
  expectClientCreate();
  ActiveTestRequest r6(*this, 0, false);

  // 6 requests, capacity [3, 2, 6] - the first cancel should tear down the client with [3]
  // since we destroy oldest first and [3, 2] can handle the remaining 5 requests.
  {
    EXPECT_CALL(*this, onClientDestroy());
    r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }

  // 5 requests, capacity [3, 2] - no teardown
  {
    EXPECT_CALL(*this, onClientDestroy()).Times(0);
    r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
  // 4 requests, capacity [3, 2] - canceling one destroys the client with [2]
  {
    EXPECT_CALL(*this, onClientDestroy());
    r3.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }

  // 3 requests, capacity [3]. Tear down the last channel when all 3 are canceled.
  {
    EXPECT_CALL(*this, onClientDestroy()).Times(0);
    r4.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
    r5.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
  {
    EXPECT_CALL(*this, onClientDestroy());
    r6.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  }
}

/**
 * Verify that connections are drained when requested.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnections) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  InSequence s;
  cluster_->max_requests_per_connection_ = 1;

  // Test drain connections call prior to any connections being created.
  pool_->drainConnections();

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  // With max_streams == 1, the second request moves the first connection
  // to draining.
  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  // This will move the second connection to draining.
  pool_->drainConnections();

  // This will destroy the 2 draining connections.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

// Test that cluster.http2_protocol_options.max_concurrent_streams limits
// concurrent requests and causes additional connections to be created.
TEST_F(Http2ConnPoolImplTest, MaxConcurrentRequestsPerStream) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);

  InSequence s;

  {
    // Create request and complete it.
    expectClientCreate();
    ActiveTestRequest r(*this, 0, false);
    expectClientConnect(0, r);
    completeRequest(r);
  }

  // Previous request completed, so this one will re-use the connection.
  {
    ActiveTestRequest r(*this, 0, true);
    completeRequest(r);
  }

  // Two concurrent requests causes one additional connection to be created.
  {
    ActiveTestRequest r1(*this, 0, true);
    expectClientCreate();
    ActiveTestRequest r2(*this, 1, false);
    expectClientConnect(1, r2);

    // Complete one of them, and create another, and it will re-use the connection.
    completeRequest(r2);
    ActiveTestRequest r3(*this, 1, true);

    completeRequest(r1);
    completeRequest(r3);
  }

  // Create two more requests; both should use existing connections.
  {
    ActiveTestRequest r1(*this, 1, true);
    ActiveTestRequest r2(*this, 0, true);
    completeRequest(r1);
    completeRequest(r2);
  }

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_total_.value());
}

// Verifies that requests are queued up in the conn pool until the connection becomes ready.
TEST_F(Http2ConnPoolImplTest, PendingRequests) {
  InSequence s;

  // Create three requests. These should be queued up.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  expectStreamConnect(0, r1);
  expectStreamConnect(0, r2);
  expectClientConnect(0, r3);

  // Send a request through each stream.
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  r3.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  // Since we now have an active connection, subsequent requests should connect immediately.
  ActiveTestRequest r4(*this, 0, true);

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

// Verifies that the correct number of CONNECTING connections are created for
// the pending requests, when the total requests per connection is limited
TEST_F(Http2ConnPoolImplTest, PendingRequestsNumberConnectingTotalRequestsPerConnection) {
  cluster_->max_requests_per_connection_ = 2;
  InSequence s;

  // Create three requests. The 3rd should create a 2nd connection due to the limit
  // of 2 requests per connection.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  expectClientCreate();
  ActiveTestRequest r3(*this, 1, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  expectStreamConnect(0, r1);
  expectClientConnect(0, r2);
  expectClientConnect(1, r3);

  // Send a request through each stream.
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  r3.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();
}

// Verifies that the correct number of CONNECTING connections are created for
// the pending requests, when the concurrent requests per connection is limited
TEST_F(Http2ConnPoolImplTest, PendingRequestsNumberConnectingConcurrentRequestsPerConnection) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(2);
  InSequence s;

  // Create three requests. The 3rd should create a 2nd connection due to the limit
  // of 2 requests per connection.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  expectClientCreate();
  ActiveTestRequest r3(*this, 1, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  expectStreamConnect(0, r1);
  expectClientConnect(0, r2);
  expectClientConnect(1, r3);

  // Send a request through each stream.
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  r3.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();
}

// Verifies that requests are queued up in the conn pool and fail when the connection
// fails to be established.
TEST_F(Http2ConnPoolImplTest, PendingRequestsFailure) {
  InSequence s;
  cluster_->max_requests_per_connection_ = 10;

  // Create three requests. These should be queued up.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  // Note that these occur in reverse order due to the order we purge pending requests in.
  expectStreamReset(r3);
  expectStreamReset(r2);
  expectClientReset(0, r1, false);

  expectClientCreate();
  // Since we have no active connection, subsequence requests will queue until
  // the new connection is established.
  ActiveTestRequest r4(*this, 1, false);
  expectClientConnect(1, r4);

  // Clean up everything.
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

// Verifies resets due to local connection closes are tracked correctly.
TEST_F(Http2ConnPoolImplTest, LocalFailure) {
  InSequence s;
  cluster_->max_requests_per_connection_ = 10;

  // Create three requests. These should be queued up.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  // Note that these occur in reverse order due to the order we purge pending requests in.
  expectStreamReset(r3);
  expectStreamReset(r2);
  expectClientReset(0, r1, true);

  EXPECT_CALL(*this, onClientDestroy());
}

// Verifies that requests are queued up in the conn pool and respect max request circuit breaking
// when the connection is established.
TEST_F(Http2ConnPoolImplTest, PendingRequestsRequestOverflow) {
  InSequence s;

  // Inflate the resource count to just under the limit.
  auto& requests = host_->cluster().resourceManager(Upstream::ResourcePriority::Default).requests();
  for (uint64_t i = 0; i < requests.max() - 1; ++i) {
    requests.inc();
  }

  // Create three requests. These should be queued up.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // We queued up three requests, but we can only afford one before hitting the circuit
  // breaker. Thus, we expect to see 2 resets and one successful connect.
  expectStreamConnect(0, r1);
  expectStreamReset(r2);
  expectStreamReset(r3);
  EXPECT_CALL(*test_clients_[0].connect_timer_, disableTimer());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Clean up everything.
  for (uint64_t i = 0; i < requests.max() - 1; ++i) {
    requests.dec();
  }
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

// Verifies that we honor the max pending requests circuit breaker.
TEST_F(Http2ConnPoolImplTest, PendingRequestsMaxPendingCircuitBreaker) {
  InSequence s;

  // Inflate the resource count to just under the limit.
  auto& pending_reqs =
      host_->cluster().resourceManager(Upstream::ResourcePriority::Default).pendingRequests();
  for (uint64_t i = 0; i < pending_reqs.max() - 1; ++i) {
    pending_reqs.inc();
  }

  // Create two requests. The first one should be enqueued, while the second one
  // should fail fast due to us being above the max pending requests limit.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);

  MockResponseDecoder decoder;
  ConnPoolCallbacks callbacks;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_->newStream(decoder, callbacks));

  expectStreamConnect(0, r1);
  EXPECT_CALL(*test_clients_[0].connect_timer_, disableTimer());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Clean up everything.
  for (uint64_t i = 0; i < pending_reqs.max() - 1; ++i) {
    pending_reqs.dec();
  }
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, VerifyConnectionTimingStats) {
  InSequence s;
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_connect_ms"), _));
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

/**
 * Test that buffer limits are set.
 */
TEST_F(Http2ConnPoolImplTest, VerifyBufferLimits) {
  InSequence s;
  expectClientCreate(8192);
  ActiveTestRequest r1(*this, 0, false);

  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, RequestAndResponse) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_active_.value());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  ActiveTestRequest r2(*this, 0, true);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_active_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, LocalReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, false);
  r1.callbacks_.outer_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_tx_reset_.value());
  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());
  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_active_.value());
}

TEST_F(Http2ConnPoolImplTest, RemoteReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, false);
  r1.inner_encoder_.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_rx_reset_.value());
  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());
  EXPECT_EQ(0U, cluster_->stats_.upstream_cx_active_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectWithActiveRequest) {
  InSequence s;
  cluster_->max_requests_per_connection_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  ReadyWatcher drained;
  pool_->addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectDrainingWithActiveRequest) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  InSequence s;
  cluster_->max_requests_per_connection_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  ReadyWatcher drained;
  pool_->addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainPrimary) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  InSequence s;
  cluster_->max_requests_per_connection_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  ReadyWatcher drained;
  pool_->addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, DrainPrimaryNoActiveRequest) {
  cluster_->resetResourceManager(2, 1024, 1024, 1, 1);

  InSequence s;
  cluster_->max_requests_per_connection_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  ReadyWatcher drained;
  EXPECT_CALL(drained, ready());
  pool_->addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, ConnectTimeout) {
  InSequence s;

  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  EXPECT_CALL(r1.callbacks_.pool_failure_, ready());
  test_clients_[0].connect_timer_->invokeCallback();
  EXPECT_EQ(r1.callbacks_.reason_, ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_total_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_pending_failure_eject_.value());
  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_local_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, MaxGlobalRequests) {
  cluster_->resetResourceManager(1024, 1024, 1, 1, 1);
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);

  ConnPoolCallbacks callbacks;
  MockResponseDecoder decoder;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_->newStream(decoder, callbacks));

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, GoAway) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[0].codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(
      TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_close_notify_.value());
}

TEST_F(Http2ConnPoolImplTest, NoActiveConnectionsByDefault) {
  EXPECT_FALSE(pool_->hasActiveConnections());
}

// Show that an active request on the primary connection is considered active.
TEST_F(Http2ConnPoolImplTest, ActiveConnectionsHasActiveRequestsTrue) {
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);

  EXPECT_TRUE(pool_->hasActiveConnections());

  completeRequestCloseUpstream(0, r1);
}

// Show that pending requests are considered active.
TEST_F(Http2ConnPoolImplTest, PendingRequestsConsideredActive) {
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);

  EXPECT_TRUE(pool_->hasActiveConnections());

  expectClientConnect(0, r1);
  completeRequestCloseUpstream(0, r1);
}

// Show that even if there is a primary client still, if all of its requests have completed, then it
// does not have any active connections.
TEST_F(Http2ConnPoolImplTest, ResponseCompletedConnectionReadyNoActiveConnections) {
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  completeRequest(r1);

  EXPECT_FALSE(pool_->hasActiveConnections());

  closeClient(0);
}

// Show that if connections are draining, they're still considered active.
TEST_F(Http2ConnPoolImplTest, DrainingConnectionsConsideredActive) {
  cluster_->max_requests_per_connection_ = 1;
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  pool_->drainConnections();

  EXPECT_TRUE(pool_->hasActiveConnections());

  completeRequest(r1);
  closeClient(0);
}

// Show that once we've drained all connections, there are no longer any active.
TEST_F(Http2ConnPoolImplTest, DrainedConnectionsNotActive) {
  cluster_->max_requests_per_connection_ = 1;
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  pool_->drainConnections();
  completeRequest(r1);

  EXPECT_FALSE(pool_->hasActiveConnections());

  closeClient(0);
}
} // namespace Http2
} // namespace Http
} // namespace Envoy
