#include <cstdint>
#include <memory>
#include <vector>

#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/http2/conn_pool.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/http_server_properties_cache.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/transport_socket_match.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
namespace Http2 {

class TestConnPoolImpl : public FixedHttpConnPoolImpl {
public:
  TestConnPoolImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                   Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                   const Network::ConnectionSocket::OptionsSharedPtr& options,
                   const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                   Envoy::Upstream::ClusterConnectivityState& state)
      : FixedHttpConnPoolImpl(
            std::move(host), std::move(priority), dispatcher, options, transport_socket_options,
            random_generator, state,
            [](HttpConnPoolImplBase* pool) {
              return std::make_unique<ActiveClient>(*pool, absl::nullopt);
            },
            [](Upstream::Host::CreateConnectionData&, HttpConnPoolImplBase*) { return nullptr; },
            std::vector<Protocol>{Protocol::Http2}) {}

  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override {
    // We expect to own the connection, but already have it, so just release it to prevent it from
    // getting deleted.
    data.connection_.release();
    return CodecClientPtr{createCodecClient_(data)};
  }

  MOCK_METHOD(CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData & data));
};

class ActiveTestRequest;

class Http2ConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
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
        upstream_ready_cb_(new NiceMock<Event::MockSchedulableCallback>(&dispatcher_)),
        pool_(std::make_unique<TestConnPoolImpl>(dispatcher_, random_, host_,
                                                 Upstream::ResourcePriority::Default, nullptr,
                                                 nullptr, state_)) {
    // Default connections to 1024 because the tests shouldn't be relying on the
    // connection resource limit for most tests.
    cluster_->resetResourceManager(1024, 1024, 1024, 1, 1);
  }

  ~Http2ConnPoolImplTest() override {
    EXPECT_EQ("", TestUtility::nonZeroedGauges(cluster_->stats_store_.gauges()));
  }

  void createTestClients(int num_clients) {
    // Create N clients.
    for (int i = 0; i < num_clients; ++i) {
      test_clients_.emplace_back();
      TestCodecClient& test_client = test_clients_.back();
      test_client.connection_ = new NiceMock<Network::MockClientConnection>();
      test_client.codec_ = new NiceMock<Http::MockClientConnection>();
      test_client.connect_timer_ = new NiceMock<Event::MockTimer>();
      test_client.client_dispatcher_ = api_->allocateDispatcher("test_thread");
    }

    // Outside the for loop, set the createTimer expectations.
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .Times(num_clients)
        .WillRepeatedly(Invoke([this](Event::TimerCb cb) {
          test_clients_[timer_index_].connect_timer_->callback_ = cb;
          return test_clients_[timer_index_++].connect_timer_;
        }));
    // Loop again through the last num_clients entries to set enableTimer expectations.
    // Ideally this could be done in the loop above but it breaks InSequence
    // assertions.
    for (size_t i = test_clients_.size() - num_clients; i < test_clients_.size(); ++i) {
      TestCodecClient& test_client = test_clients_[i];
      EXPECT_CALL(*test_client.connect_timer_, enableTimer(_, _));
    }
  }

  void expectConnectionSetupForClient(int num_clients,
                                      absl::optional<uint32_t> buffer_limits = {}) {
    // Set the createClientConnection mocks. The createCodecClient_ invoke
    // below takes care of making sure connection_index_ is updated.
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .Times(num_clients)
        .WillRepeatedly(InvokeWithoutArgs([this]() -> Network::ClientConnection* {
          return test_clients_[connection_index_].connection_;
        }));

    // Loop through the last num_clients clients, setting up codec clients and
    // per-client mocks.
    for (size_t i = test_clients_.size() - num_clients; i < test_clients_.size(); ++i) {
      TestCodecClient& test_client = test_clients_[i];
      auto cluster = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
      Network::ClientConnectionPtr connection{test_client.connection_};
      test_client.codec_client_ = new CodecClientForTest(
          CodecType::HTTP1, std::move(connection), test_client.codec_,
          [this](CodecClient*) -> void { onClientDestroy(); },
          Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000", simTime()),
          *test_client.client_dispatcher_);
      if (buffer_limits) {
        EXPECT_CALL(*cluster_, perConnectionBufferLimitBytes())
            .Times(num_clients)
            .WillRepeatedly(Return(*buffer_limits));
        EXPECT_CALL(*test_client.connection_, setBufferLimits(*buffer_limits));
      }
    }
    // Finally (for InSequence tests) set up createCodecClient and make sure the
    // index is incremented to avoid returning the same client more than once.
    EXPECT_CALL(*pool_, createCodecClient_(_))
        .Times(num_clients)
        .WillRepeatedly(Invoke([this](Upstream::Host::CreateConnectionData&) -> CodecClient* {
          return test_clients_[connection_index_++].codec_client_;
        }));
  }

  // Creates a new test client, expecting a new connection to be created and associated
  // with the new client.
  void expectClientCreate(absl::optional<uint32_t> buffer_limits = {}) {
    createTestClients(1);
    expectConnectionSetupForClient(1, buffer_limits);
  }
  void expectClientsCreate(int num_clients) {
    createTestClients(num_clients);
    expectConnectionSetupForClient(num_clients, absl::nullopt);
  }

  // Connects a pending connection for client with the given index.
  void expectClientConnect(size_t index);
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

// Use a macro to avoid tons of cut and paste, but to retain line numbers on error.
#define CHECK_STATE(active, pending, capacity)                                                     \
  EXPECT_EQ(state_.pending_streams_, pending);                                                     \
  EXPECT_EQ(state_.active_streams_, active);                                                       \
  EXPECT_EQ(state_.connecting_and_connected_stream_capacity_, capacity);

  /**
   * Closes a test client.
   */
  void closeClient(size_t index);

  /**
   * Closes all test clients.
   */
  void closeAllClients();

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

  Upstream::ClusterConnectivityState state_;
  int timer_index_{};
  int connection_index_{};
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80", simTime())};
  NiceMock<Event::MockSchedulableCallback>* upstream_ready_cb_;
  std::unique_ptr<TestConnPoolImpl> pool_;
  std::vector<TestCodecClient> test_clients_;
  NiceMock<Runtime::MockLoader> runtime_;
  Random::MockRandomGenerator random_;
};

class ActiveTestRequest {
public:
  ActiveTestRequest(Http2ConnPoolImplTest& test, size_t client_index, bool expect_connected) {
    if (expect_connected) {
      EXPECT_CALL(*test.test_clients_[client_index].codec_, newStream(_))
          .WillOnce(DoAll(SaveArgAddress(&inner_decoder_), ReturnRef(inner_encoder_)));
      EXPECT_CALL(callbacks_.pool_ready_, ready());
      EXPECT_EQ(nullptr, test.pool_->newStream(decoder_, callbacks_,
                                               {/*can_send_early_data_=*/false,
                                                /*can_use_http3_=*/false}));
    } else {
      handle_ = test.pool_->newStream(decoder_, callbacks_,
                                      {/*can_send_early_data_=*/false,
                                       /*can_use_http3_=*/false});
      EXPECT_NE(nullptr, handle_);
    }
  }

  MockResponseDecoder decoder_;
  ConnPoolCallbacks callbacks_;
  ResponseDecoder* inner_decoder_{};
  NiceMock<MockRequestEncoder> inner_encoder_;
  ConnectionPool::Cancellable* handle_{};
};

void Http2ConnPoolImplTest::expectClientConnect(size_t index) {
  test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::Connected);
}

void Http2ConnPoolImplTest::expectClientConnect(size_t index, ActiveTestRequest& r) {
  expectStreamConnect(index, r);
  expectClientConnect(index);
}

void Http2ConnPoolImplTest::expectStreamConnect(size_t index, ActiveTestRequest& r) {
  EXPECT_CALL(*test_clients_[index].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r.inner_decoder_), ReturnRef(r.inner_encoder_)));
  EXPECT_CALL(r.callbacks_.pool_ready_, ready());
}

void Http2ConnPoolImplTest::expectClientReset(size_t index, ActiveTestRequest& r,
                                              bool local_failure) {
  expectStreamReset(r);
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

void Http2ConnPoolImplTest::closeAllClients() {
  for (auto& test_client : test_clients_) {
    test_client.connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  }
  EXPECT_CALL(*this, onClientDestroy()).Times(test_clients_.size());
  dispatcher_.clearDeferredDeleteList();
}

void Http2ConnPoolImplTest::completeRequest(ActiveTestRequest& r) {
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
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
  host_ = Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80", simTime());
  new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  pool_ = std::make_unique<TestConnPoolImpl>(
      dispatcher_, random_, host_, Upstream::ResourcePriority::Default, nullptr, nullptr, state_);

  // This requires some careful set up of expectations ordering: the call to createTransportSocket
  // happens before all the connection set up but after the test client is created (due to some)
  // of the mocks that are constructed as part of the test client.
  createTestClients(1);
  EXPECT_CALL(*factory_ptr, createTransportSocket(_, _))
      .WillOnce(Invoke([](Network::TransportSocketOptionsConstSharedPtr options,
                          Upstream::HostDescriptionConstSharedPtr) -> Network::TransportSocketPtr {
        EXPECT_TRUE(options != nullptr);
        EXPECT_EQ(options->applicationProtocolFallback()[0], Http::Utility::AlpnNames::get().Http2);
        return std::make_unique<Network::RawBufferSocket>();
      }));
  expectConnectionSetupForClient(1);
  ActiveTestRequest r(*this, 0, false);
  expectClientConnect(0, r);
  EXPECT_CALL(r.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

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
  EXPECT_TRUE(
      r.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

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
  EXPECT_TRUE(
      r.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

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
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

  // Cancel the pending request, and then the connection can be closed.
  r.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::Default);
  EXPECT_CALL(*this, onClientDestroy());
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
}

/**
 * Verify that on CloseExcess, the connection is destroyed immediately.
 */
TEST_F(Http2ConnPoolImplTest, CloseExcess) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r(*this, 0, false);

  // Pending request prevents the connection from being drained
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

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
  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(3));
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(2));
  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(2));
  expectClientCreate();
  ActiveTestRequest r4(*this, 0, false);
  ActiveTestRequest r5(*this, 0, false);

  EXPECT_CALL(*cluster_, maxRequestsPerConnection).WillOnce(Return(6));
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
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  // With max_streams == 1, the second request moves the first connection
  // to draining.
  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  // This will move the second connection to draining.
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

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
TEST_F(Http2ConnPoolImplTest, PendingStreams) {
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r3.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

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
TEST_F(Http2ConnPoolImplTest, PendingStreamsNumberConnectingTotalRequestsPerConnection) {
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r3.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();
}

// Verifies that the correct number of CONNECTING connections are created for
// the pending requests, when the concurrent requests per connection is limited
TEST_F(Http2ConnPoolImplTest, PendingStreamsNumberConnectingConcurrentRequestsPerConnection) {
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r3.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();
}

// Verifies that requests are queued up in the conn pool and fail when the connection
// fails to be established.
TEST_F(Http2ConnPoolImplTest, PendingStreamsFailure) {
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
TEST_F(Http2ConnPoolImplTest, PendingStreamsRequestOverflow) {
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
  expectClientConnect(0);

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
TEST_F(Http2ConnPoolImplTest, PendingStreamsMaxPendingCircuitBreaker) {
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
  EXPECT_EQ(nullptr, pool_->newStream(decoder, callbacks,
                                      {/*can_send_early_data_=*/false,
                                       /*can_use_http3_=*/false}));

  expectStreamConnect(0, r1);
  expectClientConnect(0);

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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
  // 1 stream. HTTP/2 defaults to 536870912 streams/connection.
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 536870912 /*capacity*/);

  expectClientConnect(0, r1);
  // capacity goes down by one as one stream is used.
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 536870911 /*capacity*/);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*capacity*/);

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, RequestAndResponse) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_active_.value());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  ActiveTestRequest r2(*this, 0, true);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, false)
          .ok());
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, false)
          .ok());
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  ReadyWatcher drained;
  pool_->addIdleCallback([&]() -> void { drained.ready(); });
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  ReadyWatcher drained;
  pool_->addIdleCallback([&]() -> void { drained.ready(); });
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());

  ReadyWatcher drained;
  pool_->addIdleCallback([&]() -> void { drained.ready(); });
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready()).Times(AtLeast(1));
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  ReadyWatcher drained;
  EXPECT_CALL(drained, ready());
  pool_->addIdleCallback([&]() -> void { drained.ready(); });
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

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
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  ConnPoolCallbacks callbacks;
  MockResponseDecoder decoder;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_->newStream(decoder, callbacks,
                                      {/*can_send_early_data_=*/false,
                                       /*can_use_http3_=*/false}));

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
  EXPECT_TRUE(
      r1.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, true);

  test_clients_[0].codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  EXPECT_TRUE(
      r2.callbacks_.outer_encoder_
          ->encodeHeaders(TestRequestHeaderMapImpl{{":path", "/"}, {":method", "GET"}}, true)
          .ok());
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
TEST_F(Http2ConnPoolImplTest, PendingStreamsConsideredActive) {
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
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

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
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  completeRequest(r1);

  EXPECT_FALSE(pool_->hasActiveConnections());

  closeClient(0);
}

TEST_F(Http2ConnPoolImplTest, PreconnectWithoutMultiplexing) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // With one request per connection, and preconnect 1.5, the first request will
  // kick off 2 connections.
  expectClientsCreate(2);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*capacity*/);

  // With another incoming request, we'll have 2 in flight and want 1.5*2 so
  // create one connection.
  expectClientsCreate(1);
  ActiveTestRequest r2(*this, 0, false);
  CHECK_STATE(0 /*active*/, 2 /*pending*/, 3 /*capacity*/);

  // With a third request we'll have 3 in flight and want 1.5*3 -> 5 so kick off
  // two again.
  expectClientsCreate(2);
  ActiveTestRequest r3(*this, 0, false);
  CHECK_STATE(0 /*active*/, 3 /*pending*/, 5 /*capacity*/);

  r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  r3.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);

  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, IncreaseCapacityWithSettingsFrame) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(100);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1));

  // The initial capacity is determined by configured max_concurrent_streams.
  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 100 /*capacity*/);

  // When the connection connects, there is 99 spare capacity in this pool.
  EXPECT_CALL(*test_clients_[0].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r1.inner_decoder_), ReturnRef(r1.inner_encoder_)));
  EXPECT_CALL(r1.callbacks_.pool_ready_, ready());
  expectClientConnect(0);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 99 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 1);

  // Settings frame results in 0 capacity, the state of client changes from Ready to BUSY.
  NiceMock<MockReceivedSettings> settings;
  settings.max_concurrent_streams_ = 1;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 0 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 0);

  // Settings frame results in 9 capacity, the state of client changes from BUSY to Ready.
  settings.max_concurrent_streams_ = 10;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 9 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 1);

  // Settings frame with capacity of 150 which will be restricted by configured
  // max_concurrent_streams, then results in 99 capacity.
  settings.max_concurrent_streams_ = 150;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 99 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 1);

  // Close connection.
  closeAllClients();
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*capacity*/);
}

TEST_F(Http2ConnPoolImplTest, DisconnectWithNegativeCapacity) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(6);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1));

  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 6 /*capacity*/);
  ActiveTestRequest r2(*this, 0, false);
  CHECK_STATE(0 /*active*/, 2 /*pending*/, 6 /*capacity*/);
  ActiveTestRequest r3(*this, 0, false);
  CHECK_STATE(0 /*active*/, 3 /*pending*/, 6 /*capacity*/);
  ActiveTestRequest r4(*this, 0, false);
  CHECK_STATE(0 /*active*/, 4 /*pending*/, 6 /*capacity*/);
  ActiveTestRequest r5(*this, 0, false);
  CHECK_STATE(0 /*active*/, 5 /*pending*/, 6 /*capacity*/);

  // When the connection connects, there is 1 spare capacity in this pool.
  EXPECT_CALL(*test_clients_[0].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r1.inner_decoder_), ReturnRef(r1.inner_encoder_)))
      .WillOnce(DoAll(SaveArgAddress(&r2.inner_decoder_), ReturnRef(r2.inner_encoder_)))
      .WillOnce(DoAll(SaveArgAddress(&r3.inner_decoder_), ReturnRef(r3.inner_encoder_)))
      .WillOnce(DoAll(SaveArgAddress(&r4.inner_decoder_), ReturnRef(r4.inner_encoder_)))
      .WillOnce(DoAll(SaveArgAddress(&r5.inner_decoder_), ReturnRef(r5.inner_encoder_)));
  EXPECT_CALL(r1.callbacks_.pool_ready_, ready());
  EXPECT_CALL(r2.callbacks_.pool_ready_, ready());
  EXPECT_CALL(r3.callbacks_.pool_ready_, ready());
  EXPECT_CALL(r4.callbacks_.pool_ready_, ready());
  EXPECT_CALL(r5.callbacks_.pool_ready_, ready());
  expectClientConnect(0);
  CHECK_STATE(5 /*active*/, 0 /*pending*/, 1 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 1);

  // Settings frame results in -2 capacity.
  NiceMock<MockReceivedSettings> settings;
  settings.max_concurrent_streams_ = 3;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(5 /*active*/, 0 /*pending*/, -2 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 0);

  // Close 1 stream, concurrency capacity goes to -1, still no ready client available.
  completeRequest(r1);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 0);

  // Close 2 streams, concurrency capacity goes to 1, there should be one ready client.
  completeRequest(r2);
  completeRequest(r3);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 1);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Another settings frame results in -1 capacity.
  settings.max_concurrent_streams_ = 1;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, -1 /*capacity*/);
  EXPECT_EQ(pool_->owningList(Envoy::ConnectionPool::ActiveClient::State::Ready).size(), 0);

  // Close connection with negative capacity.
  closeAllClients();
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*capacity*/);
}

TEST_F(Http2ConnPoolImplTest, PreconnectWithMultiplexing) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(2);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // With two requests per connection, and preconnect 1.5, the first request will
  // only kick off 1 connection.
  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*capacity*/);

  // With another incoming request, we'll have capacity(2) in flight and want 1.5*2 so
  // create an additional connection.
  expectClientsCreate(1);
  ActiveTestRequest r2(*this, 0, false);
  CHECK_STATE(0 /*active*/, 2 /*pending*/, 4 /*capacity*/);

  // Clean up.
  r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, PreconnectWithSettings) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(2);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // With two requests per connection, and preconnect 1.5, the first request will
  // only kick off 1 connection.
  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*capacity*/);

  // With another incoming request, we'll have capacity(2) in flight and want 1.5*2 so
  // create an additional connection.
  expectClientsCreate(1);
  ActiveTestRequest r2(*this, 0, false);
  CHECK_STATE(0 /*active*/, 2 /*pending*/, 4 /*capacity*/);

  // Connect, so we can receive SETTINGS.
  EXPECT_CALL(*test_clients_[0].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r1.inner_decoder_), ReturnRef(r1.inner_encoder_)))
      .WillOnce(DoAll(SaveArgAddress(&r2.inner_decoder_), ReturnRef(r2.inner_encoder_)));
  EXPECT_CALL(r1.callbacks_.pool_ready_, ready());
  EXPECT_CALL(r2.callbacks_.pool_ready_, ready());
  expectClientConnect(0);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 2 /*capacity*/);
  expectClientConnect(1);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 2 /*capacity*/);

  // Now have the codecs receive SETTINGS frames limiting the streams per connection to one.
  // onSettings
  NiceMock<MockReceivedSettings> settings;
  settings.max_concurrent_streams_ = 1;
  test_clients_[0].codec_client_->onSettings(settings);
  test_clients_[1].codec_client_->onSettings(settings);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 0 /*capacity*/);

  // Clean up.
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, PreconnectWithGoaway) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(2);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // With two requests per connection, and preconnect 1.5, the first request will
  // only kick off 1 connection.
  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*capacity*/);

  EXPECT_CALL(*test_clients_[0].codec_, newStream(_))
      .WillOnce(DoAll(SaveArgAddress(&r1.inner_decoder_), ReturnRef(r1.inner_encoder_)));
  EXPECT_CALL(r1.callbacks_.pool_ready_, ready());
  expectClientConnect(0);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Send a goaway. This does not currently trigger preconnect, but will update
  // the capacity.
  test_clients_[0].codec_client_->raiseGoAway(Http::GoAwayErrorCode::Other);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 0 /*capacity*/);

  // Clean up.
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, PreconnectEvenWhenReady) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  // With one request per connection, and preconnect 1.5, the first request will
  // kick off 2 connections.
  expectClientsCreate(2);
  ActiveTestRequest r1(*this, 0, false);

  // When the first client connects, r1 will be assigned.
  expectClientConnect(0, r1);
  // When the second connects, there is no waiting stream request to assign.
  expectClientConnect(1);

  // The next incoming request will immediately be assigned a stream, and also
  // kick off a preconnect.
  expectClientsCreate(1);
  ActiveTestRequest r2(*this, 1, true);

  // Clean up.
  completeRequest(r1);
  completeRequest(r2);
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, PreconnectAfterTimeout) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  expectClientsCreate(2);
  ActiveTestRequest r1(*this, 0, false);

  // When the first client connects, r1 will be assigned.
  expectClientConnect(0, r1);

  // Now cause the preconnected connection to fail. We should try to create
  // another in its place.
  expectClientsCreate(1);
  test_clients_[1].connect_timer_->invokeCallback();

  // Clean up.
  completeRequest(r1);
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, CloseExcessWithPreconnect) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(1);
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.00));

  // First request preconnects an additional connection.
  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);

  // Second request does not preconnect.
  expectClientsCreate(1);
  ActiveTestRequest r2(*this, 0, false);

  // Change the preconnect ratio to force the connection to no longer be excess.
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(2));
  // Closing off the second request should bring us back to 1 request in queue,
  // desired capacity 2, so will not close the connection.
  EXPECT_CALL(*this, onClientDestroy()).Times(0);
  r2.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);

  // Clean up.
  r1.handle_->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

// Test that maybePreconnect is passed up to the base class implementation.
TEST_F(Http2ConnPoolImplTest, MaybePreconnect) {
  ON_CALL(*cluster_, perUpstreamPreconnectRatio).WillByDefault(Return(1.5));

  EXPECT_FALSE(pool_->maybePreconnect(0));

  expectClientsCreate(1);
  EXPECT_TRUE(pool_->maybePreconnect(2));

  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
}

TEST_F(Http2ConnPoolImplTest, TestUnusedCapacity) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(8);
  cluster_->max_requests_per_connection_ = 6;

  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  // Initially, capacity is based on remaining streams and capped at 6.
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 6 /*capacity*/);
  expectClientConnect(0, r1);
  // Now the stream is active, remaining concurrency capacity is 5.
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 5 /*capacity*/);

  // With two more streams, remaining unused capacity is 3.
  ActiveTestRequest r2(*this, 0, true);
  ActiveTestRequest r3(*this, 0, true);
  CHECK_STATE(3 /*active*/, 0 /*pending*/, 3 /*capacity*/);

  // Settings frame results in 1 unused capacity.
  NiceMock<MockReceivedSettings> settings;
  settings.max_concurrent_streams_ = 4;
  test_clients_[0].codec_client_->onSettings(settings);
  CHECK_STATE(3 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Closing a stream, unused capacity returns to 2.
  completeRequest(r1);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 2 /*capacity*/);

  // Closing another, unused capacity returns to 3 (3 remaining stream).
  completeRequest(r2);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 3 /*capacity*/);

  // Closing the last stream, unused capacity remains at 3, as there is only 3 remaining streams.
  completeRequest(r3);
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 3 /*capacity*/);

  // Clean up with an outstanding stream.
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*capacity*/);
}

TEST_F(Http2ConnPoolImplTest, TestStateWithMultiplexing) {
  cluster_->http2_options_.mutable_max_concurrent_streams()->set_value(2);
  cluster_->max_requests_per_connection_ = 4;

  expectClientsCreate(1);
  ActiveTestRequest r1(*this, 0, false);
  // Initially, capacity is based on concurrency and capped at 2.
  CHECK_STATE(0 /*active*/, 1 /*pending*/, 2 /*capacity*/);
  expectClientConnect(0, r1);
  // Now the stream is active, remaining concurrency capacity is 1.
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // With one more stream, remaining concurrency capacity is 0.
  ActiveTestRequest r2(*this, 0, true);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 0 /*capacity*/);

  // If one stream closes, concurrency capacity goes to 1 (2 remaining streams).
  completeRequest(r1);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Assigning a new stream, concurrency capacity returns to 0 (1 remaining stream).
  ActiveTestRequest r3(*this, 0, true);
  CHECK_STATE(2 /*active*/, 0 /*pending*/, 0 /*capacity*/);

  // Closing a stream, capacity returns to 1 (both concurrency and remaining streams).
  completeRequest(r2);
  CHECK_STATE(1 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Closing another, capacity remains at 1, as there is only 1 remaining stream.
  completeRequest(r3);
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 1 /*capacity*/);

  // Clean up with an outstanding stream.
  pool_->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
  closeAllClients();
  CHECK_STATE(0 /*active*/, 0 /*pending*/, 0 /*capacity*/);
}

class InitialStreamsLimitTest : public Http2ConnPoolImplTest {
protected:
  void SetUp() override {
    mock_host_->cluster_.http2_options_.mutable_max_concurrent_streams()->set_value(2000);
    EXPECT_CALL(mock_host_->cluster_, maxRequestsPerConnection)
        .Times(AnyNumber())
        .WillRepeatedly(Return(4000));
    EXPECT_CALL(mock_host_->cluster_, maxRequestsPerConnection()).WillRepeatedly(Return(8000));
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.allow_concurrency_for_alpn_pool", "true"}});
  }

  TestScopedRuntime scoped_runtime_;
  absl::optional<HttpServerPropertiesCache::Origin> origin_{{"https", "hostname.com", 443}};
  std::shared_ptr<Upstream::MockHost> mock_host_{std::make_shared<NiceMock<Upstream::MockHost>>()};
  std::shared_ptr<MockHttpServerPropertiesCache> cache_{
      std::make_shared<NiceMock<MockHttpServerPropertiesCache>>()};
};

TEST_F(InitialStreamsLimitTest, InitialStreamsLimitDefaultsNoCache) {
  // By default, return max concurrent streams from SetUp.
  cache_ = nullptr;
  EXPECT_EQ(2000, ActiveClient::calculateInitialStreamsLimit(nullptr, origin_, mock_host_));
}

TEST_F(InitialStreamsLimitTest, InitialStreamsLimitDefaultsWithCache) {
  // Adding a cache is a no-op if there's no cached settings.
  EXPECT_EQ(2000, ActiveClient::calculateInitialStreamsLimit(cache_, origin_, mock_host_));
}

TEST_F(InitialStreamsLimitTest, InitialStreamsLimitZeroCached) {
  // Zero is ignored.
  EXPECT_CALL(*cache_, getConcurrentStreams(_)).WillOnce(Return(0));
  EXPECT_EQ(2000, ActiveClient::calculateInitialStreamsLimit(cache_, origin_, mock_host_));
}

TEST_F(InitialStreamsLimitTest, InitialStreamsLimitRespectCache) {
  // Cached settings are respected if lower than configured streams.
  EXPECT_CALL(*cache_, getConcurrentStreams(_)).WillOnce(Return(500));
  EXPECT_EQ(500, ActiveClient::calculateInitialStreamsLimit(cache_, origin_, mock_host_));
}

TEST_F(InitialStreamsLimitTest, InitialStreamsLimitRespectMaxRequests) {
  // Max requests per connection is an upper bound.
  EXPECT_CALL(*cache_, getConcurrentStreams(_)).WillOnce(Return(500));
  EXPECT_CALL(mock_host_->cluster_, maxRequestsPerConnection)
      .Times(AnyNumber())
      .WillRepeatedly(Return(100));
  EXPECT_EQ(100, ActiveClient::calculateInitialStreamsLimit(cache_, origin_, mock_host_));
}
TEST_F(InitialStreamsLimitTest, InitialStreamsLimitOld) {
  // All these bounds are ignored with the reloadable feature off.
  EXPECT_CALL(*cache_, getConcurrentStreams(_)).Times(0);
  EXPECT_CALL(mock_host_->cluster_, maxRequestsPerConnection)
      .Times(AnyNumber())
      .WillRepeatedly(Return(100));
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.allow_concurrency_for_alpn_pool", "false"}});
  EXPECT_EQ(2000, ActiveClient::calculateInitialStreamsLimit(nullptr, origin_, mock_host_));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
