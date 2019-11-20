#include <cstdint>
#include <memory>
#include <vector>

#include "common/event/dispatcher_impl.h"
#include "common/http/http2/conn_pool.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
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

  MOCK_METHOD1(createCodecClient_, CodecClient*(Upstream::Host::CreateConnectionData& data));

  uint32_t maxTotalStreams() override { return max_streams_; }

  uint32_t max_streams_{std::numeric_limits<uint32_t>::max()};
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
        pool_(dispatcher_, host_, Upstream::ResourcePriority::Default, nullptr, nullptr) {}

  ~Http2ConnPoolImplTest() override {
    EXPECT_TRUE(TestUtility::gaugesZeroed(cluster_->stats_store_.gauges()));
  }

  // Creates a new test client, expecting a new connection to be created and associated
  // with the new client.
  void expectClientCreate(absl::optional<uint32_t> buffer_limits = {}) {
    test_clients_.emplace_back();
    TestCodecClient& test_client = test_clients_.back();
    test_client.connection_ = new NiceMock<Network::MockClientConnection>();
    test_client.codec_ = new NiceMock<Http::MockClientConnection>();
    test_client.connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    test_client.client_dispatcher_ = api_->allocateDispatcher();
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
    EXPECT_CALL(pool_, createCodecClient_(_))
        .WillOnce(Invoke([this](Upstream::Host::CreateConnectionData&) -> CodecClient* {
          return test_clients_.back().codec_client_;
        }));
    EXPECT_CALL(*test_client.connect_timer_, enableTimer(_, _));
  }

  // Connects a pending connection for client with the given index, asserting
  // that the provided request receives onPoolReady.
  void expectClientConnect(size_t index, ActiveTestRequest& r);
  // Asserts that onPoolReady is called on the request.
  void expectStreamConnect(size_t index, ActiveTestRequest& r);

  // Resets the connection belonging to the provided index, asserting that the
  // provided request receives onPoolFailure.
  void expectClientReset(size_t index, ActiveTestRequest& r);
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

  MOCK_METHOD0(onClientDestroy, void());

  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80")};
  TestConnPoolImpl pool_;
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
      EXPECT_EQ(nullptr, test.pool_.newStream(decoder_, callbacks_));
    } else {
      EXPECT_NE(nullptr, test.pool_.newStream(decoder_, callbacks_));
    }
  }

  Http::MockStreamDecoder decoder_;
  ConnPoolCallbacks callbacks_;
  Http::StreamDecoder* inner_decoder_{};
  NiceMock<Http::MockStreamEncoder> inner_encoder_;
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

void Http2ConnPoolImplTest::expectClientReset(size_t index, ActiveTestRequest& r) {
  expectStreamReset(r);
  EXPECT_CALL(*test_clients_[0].connect_timer_, disableTimer());
  test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
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
  r.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r.decoder_, decodeHeaders_(_, true));
  r.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);
}

void Http2ConnPoolImplTest::completeRequestCloseUpstream(size_t index, ActiveTestRequest& r) {
  completeRequest(r);
  closeClient(index);
}

/**
 * Verify that the pool retains and returns the host it was constructed with.
 */
TEST_F(Http2ConnPoolImplTest, Host) { EXPECT_EQ(host_, pool_.host()); }

/**
 * Verify that connections are drained when requested.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnections) {
  InSequence s;
  pool_.max_streams_ = 1;

  // Test drain connections call prior to any connections being created.
  pool_.drainConnections();

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  // This will move primary to draining and destroy draining.
  pool_.drainConnections();
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  // This will destroy draining.
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(2U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  EXPECT_CALL(r3.inner_encoder_, encodeHeaders(_, true));
  r3.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  // Since we now have an active connection, subsequent requests should connect immediately.
  ActiveTestRequest r4(*this, 0, true);

  // Clean up everything.
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

// Verifies that requests are queued up in the conn pool and fail when the connection
// fails to be established.
TEST_F(Http2ConnPoolImplTest, PendingRequestsFailure) {
  InSequence s;
  pool_.max_streams_ = 10;

  // Create three requests. These should be queued up.
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  ActiveTestRequest r2(*this, 0, false);
  ActiveTestRequest r3(*this, 0, false);

  // The connection now becomes ready. This should cause all the queued requests to be sent.
  // Note that these occur in reverse order due to the order we purge pending requests in.
  expectStreamReset(r3);
  expectStreamReset(r2);
  expectClientReset(0, r1);

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

  Http::MockStreamDecoder decoder;
  ConnPoolCallbacks callbacks;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_.newStream(decoder, callbacks));

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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));
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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  ActiveTestRequest r2(*this, 0, true);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, LocalReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, false);
  r1.callbacks_.outer_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_tx_reset_.value());
  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());
}

TEST_F(Http2ConnPoolImplTest, RemoteReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, false);
  r1.inner_encoder_.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_rx_reset_.value());
  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectWithActiveRequest) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  ReadyWatcher drained;
  pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_destroy_remote_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectDrainingWithActiveRequest) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  ReadyWatcher drained;
  pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);
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
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  ReadyWatcher drained;
  pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, DrainPrimaryNoActiveRequest) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  ReadyWatcher drained;
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  pool_.addDrainedCallback([&]() -> void { drained.ready(); });

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

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, cluster_->circuit_breakers_stats_.rq_open_.value());

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);

  ConnPoolCallbacks callbacks;
  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_.newStream(decoder, callbacks));

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
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].codec_client_->raiseGoAway();

  expectClientCreate();
  ActiveTestRequest r2(*this, 1, false);
  expectClientConnect(1, r2);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_close_notify_.value());
}

TEST_F(Http2ConnPoolImplTest, NoActiveConnectionsByDefault) {
  EXPECT_FALSE(pool_.hasActiveConnections());
}

// Show that an active request on the primary connection is considered active.
TEST_F(Http2ConnPoolImplTest, ActiveConnectionsHasActiveRequestsTrue) {
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);

  EXPECT_TRUE(pool_.hasActiveConnections());

  completeRequestCloseUpstream(0, r1);
}

// Show that pending requests are considered active.
TEST_F(Http2ConnPoolImplTest, PendingRequestsConsideredActive) {
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);

  EXPECT_TRUE(pool_.hasActiveConnections());

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

  EXPECT_FALSE(pool_.hasActiveConnections());

  closeClient(0);
}

// Show that if connections are draining, they're still considered active.
TEST_F(Http2ConnPoolImplTest, DrainingConnectionsConsideredActive) {
  pool_.max_streams_ = 1;
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  pool_.drainConnections();

  EXPECT_TRUE(pool_.hasActiveConnections());

  completeRequest(r1);
  closeClient(0);
}

// Show that once we've drained all connections, there are no longer any active.
TEST_F(Http2ConnPoolImplTest, DrainedConnectionsNotActive) {
  pool_.max_streams_ = 1;
  expectClientCreate();
  ActiveTestRequest r1(*this, 0, false);
  expectClientConnect(0, r1);
  pool_.drainConnections();
  completeRequest(r1);

  EXPECT_FALSE(pool_.hasActiveConnections());

  closeClient(0);
}
} // namespace Http2
} // namespace Http
} // namespace Envoy
