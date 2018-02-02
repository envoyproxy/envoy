#include <cstdint>
#include <memory>
#include <vector>

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

using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

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

class Http2ConnPoolImplTest : public testing::Test {
public:
  struct TestCodecClient {
    Http::MockClientConnection* codec_;
    Network::MockClientConnection* connection_;
    CodecClientForTest* codec_client_;
    Event::MockTimer* connect_timer_;
  };

  Http2ConnPoolImplTest()
      : pool_(dispatcher_, host_, Upstream::ResourcePriority::Default, nullptr) {}

  ~Http2ConnPoolImplTest() {
    // Make sure all gauges are 0.
    for (const Stats::GaugeSharedPtr& gauge : cluster_->stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  void expectClientCreate() {
    test_clients_.emplace_back();
    TestCodecClient& test_client = test_clients_.back();
    test_client.connection_ = new NiceMock<Network::MockClientConnection>();
    test_client.codec_ = new NiceMock<Http::MockClientConnection>();
    test_client.connect_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);

    Network::ClientConnectionPtr connection{test_client.connection_};
    test_client.codec_client_ =
        new CodecClientForTest(std::move(connection), test_client.codec_,
                               [this](CodecClient*) -> void { onClientDestroy(); }, nullptr);

    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(test_client.connection_));
    EXPECT_CALL(pool_, createCodecClient_(_))
        .WillOnce(Invoke([this](Upstream::Host::CreateConnectionData&) -> CodecClient* {
          return test_clients_.back().codec_client_;
        }));
    EXPECT_CALL(*test_client.connect_timer_, enableTimer(_));
  }

  void expectClientConnect(size_t index) {
    EXPECT_CALL(*test_clients_[index].connect_timer_, disableTimer());
    test_clients_[index].connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  MOCK_METHOD0(onClientDestroy, void());

  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Upstream::HostSharedPtr host_{Upstream::makeTestHost(cluster_, "tcp://127.0.0.1:80")};
  TestConnPoolImpl pool_;
  std::vector<TestCodecClient> test_clients_;
  NiceMock<Runtime::MockLoader> runtime_;
};

class ActiveTestRequest {
public:
  ActiveTestRequest(Http2ConnPoolImplTest& test, size_t client_index) {
    EXPECT_CALL(*test.test_clients_[client_index].codec_, newStream(_))
        .WillOnce(DoAll(SaveArgAddress(&inner_decoder_), ReturnRef(inner_encoder_)));
    EXPECT_CALL(callbacks_.pool_ready_, ready());
    EXPECT_EQ(nullptr, test.pool_.newStream(decoder_, callbacks_));
  }

  Http::MockStreamDecoder decoder_;
  ConnPoolCallbacks callbacks_;
  Http::StreamDecoder* inner_decoder_{};
  NiceMock<Http::MockStreamEncoder> inner_encoder_;
};

/**
 * Verify that connections are drained when requested.
 */
TEST_F(Http2ConnPoolImplTest, DrainConnections) {
  InSequence s;
  pool_.max_streams_ = 1;

  // Test drain connections call prior to any connections being created.
  pool_.drainConnections();

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);

  // This will move primary to draining and destroy draining.
  pool_.drainConnections();
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  // This will destroy draining.
  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, VerifyConnectionTimingStats) {
  expectClientCreate();
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_connect_ms"), _));
  EXPECT_CALL(cluster_->stats_store_,
              deliverHistogramToSinks(Property(&Stats::Metric::name, "upstream_cx_length_ms"), _));

  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

/**
 * Test that buffer limits are set.
 */
TEST_F(Http2ConnPoolImplTest, VerifyBufferLimits) {
  expectClientCreate();
  EXPECT_CALL(*cluster_, perConnectionBufferLimitBytes()).WillOnce(Return(8192));
  EXPECT_CALL(*test_clients_.back().connection_, setBufferLimits(8192));

  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, RequestAndResponse) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  ActiveTestRequest r2(*this, 0);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, LocalReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, false);
  expectClientConnect(0);
  r1.callbacks_.outer_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_tx_reset_.value());
}

TEST_F(Http2ConnPoolImplTest, RemoteReset) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, false));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, false);
  expectClientConnect(0);
  r1.inner_encoder_.stream_.resetStream(Http::StreamResetReason::RemoteReset);

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_rx_reset_.value());
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectWithActiveRequest) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);

  ReadyWatcher drained;
  pool_.addDrainedCallback([&]() -> void { drained.ready(); });

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  EXPECT_CALL(drained, ready());
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, DrainDisconnectDrainingWithActiveRequest) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);

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
}

TEST_F(Http2ConnPoolImplTest, DrainPrimary) {
  InSequence s;
  pool_.max_streams_ = 1;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);

  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);

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
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);
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

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  test_clients_[0].connect_timer_->callback_();

  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1U, cluster_->stats_.upstream_rq_pending_failure_eject_.value());
}

TEST_F(Http2ConnPoolImplTest, MaxGlobalRequests) {
  InSequence s;
  cluster_->resource_manager_.reset(
      new Upstream::ResourceManagerImpl(runtime_, "fake_key", 1024, 1024, 1, 1));

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);

  ConnPoolCallbacks callbacks;
  Http::MockStreamDecoder decoder;
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(nullptr, pool_.newStream(decoder, callbacks));

  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy());
  dispatcher_.clearDeferredDeleteList();
}

TEST_F(Http2ConnPoolImplTest, GoAway) {
  InSequence s;

  expectClientCreate();
  ActiveTestRequest r1(*this, 0);
  EXPECT_CALL(r1.inner_encoder_, encodeHeaders(_, true));
  r1.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(0);
  EXPECT_CALL(r1.decoder_, decodeHeaders_(_, true));
  r1.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[0].codec_client_->raiseGoAway();

  expectClientCreate();
  ActiveTestRequest r2(*this, 1);
  EXPECT_CALL(r2.inner_encoder_, encodeHeaders(_, true));
  r2.callbacks_.outer_encoder_->encodeHeaders(HeaderMapImpl{}, true);
  expectClientConnect(1);
  EXPECT_CALL(r2.decoder_, decodeHeaders_(_, true));
  r2.inner_decoder_->decodeHeaders(HeaderMapPtr{new HeaderMapImpl{}}, true);

  test_clients_[1].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  test_clients_[0].connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*this, onClientDestroy()).Times(2);
  dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, cluster_->stats_.upstream_cx_close_notify_.value());
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
