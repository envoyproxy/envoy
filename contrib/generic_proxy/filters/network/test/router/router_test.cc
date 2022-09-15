#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/generic_proxy/filters/network/source/router/router.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"
#include "contrib/generic_proxy/filters/network/test/mocks/route.h"
#include "gtest/gtest.h"

using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {
namespace {

class RouterFilterTest : public testing::Test {
public:
  RouterFilterTest() {
    filter_ = std::make_shared<Router::RouterFilter>(factory_context_);
    filter_->setDecoderFilterCallbacks(mock_filter_callback_);
    ON_CALL(mock_filter_callback_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  }

  void kickOffNewUpstreamRequest(bool expect_response) {
    NiceMock<MockRouteEntry> mock_route_entry;

    filter_->setRouteEntry(&mock_route_entry);

    const std::string cluster_name = "cluster_0";

    EXPECT_CALL(mock_route_entry, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

    Buffer::OwnedImpl test_buffer;
    test_buffer.add("test");

    factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

    EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
                newConnection(_));

    filter_->onEncodingSuccess(test_buffer, expect_response);
    EXPECT_EQ(1, filter_->upstreamRequestsForTest().size());
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<MockDecoderFilterCallback> mock_filter_callback_;
  NiceMock<MockCodecFactory> mock_codec_factory_;
  NiceMock<Envoy::Event::MockDispatcher> dispatcher_;

  NiceMock<MockRouteEntry> mock_route_entry_;

  std::shared_ptr<Router::RouterFilter> filter_;
};

TEST_F(RouterFilterTest, OnStreamDecodedAndNoRouteEntry) {
  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "route_not_found");
      }));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  EXPECT_EQ(filter_->onStreamDecoded(*request), FilterStatus::StopIteration);
}

TEST_F(RouterFilterTest, OnStreamDecodedWithRouteEntry) {
  NiceMock<MockRouteEntry> mock_route_entry;

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry));
  EXPECT_CALL(mock_filter_callback_, downstreamCodec()).WillOnce(ReturnRef(mock_codec_factory_));

  auto mock_request_encoder = std::make_unique<NiceMock<MockRequestEncoder>>();
  auto raw_mock_request_encoder = mock_request_encoder.get();

  EXPECT_CALL(mock_codec_factory_, requestEncoder())
      .WillOnce(Return(ByMove(std::move(mock_request_encoder))));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  EXPECT_CALL(*raw_mock_request_encoder, encode(_, _))
      .WillOnce(Invoke([&](const Request& rq, RequestEncoderCallback& cb) {
        EXPECT_EQ(&rq, request.get());
        EXPECT_EQ(&cb, filter_.get());
      }));

  EXPECT_EQ(filter_->onStreamDecoded(*request), FilterStatus::StopIteration);
}

TEST_F(RouterFilterTest, OnUpstreamCluster) {
  NiceMock<MockRouteEntry> mock_route_entry;

  filter_->setRouteEntry(&mock_route_entry);

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test");

  // No upstream cluster.
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "cluster_not_found");
      }));

  filter_->onEncodingSuccess(test_buffer, false);
}

TEST_F(RouterFilterTest, UpstreamClusterMaintainMode) {
  NiceMock<MockRouteEntry> mock_route_entry;

  filter_->setRouteEntry(&mock_route_entry);

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test");

  factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

  // Maintain mode.
  EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
              maintenanceMode())
      .WillOnce(Return(true));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "cluster_maintain_mode");
      }));

  filter_->onEncodingSuccess(test_buffer, false);
}

TEST_F(RouterFilterTest, UpstreamClusterNoHealthyUpstream) {
  NiceMock<MockRouteEntry> mock_route_entry;

  filter_->setRouteEntry(&mock_route_entry);

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test");

  factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

  // No conn pool.
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "no_healthy_upstream");
      }));

  filter_->onEncodingSuccess(test_buffer, false);
}

TEST_F(RouterFilterTest, KickOffNormalUpstreamRequest) { kickOffNewUpstreamRequest(false); }

TEST_F(RouterFilterTest, UpstreamStreamRequestWatermarkCheck) {
  kickOffNewUpstreamRequest(false);
  // Do nothing.
  filter_->upstreamRequestsForTest().front()->onAboveWriteBufferHighWatermark();
  filter_->upstreamRequestsForTest().front()->onBelowWriteBufferLowWatermark();
}

TEST_F(RouterFilterTest, UpstreamRequestResetBeforePoolCallback) {
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(
      factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.handles_.back(),
      cancel(_));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "local_reset");
      }));

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  filter_->upstreamRequestsForTest().begin()->get()->resetStream(StreamResetReason::LocalReset);

  EXPECT_EQ(nullptr, upstream_request->conn_pool_handle_);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionOverflow) {
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "overflow");
      }));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::Overflow);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeout) {
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "connection_failure");
      }));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::Timeout);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndExpectNoResponse) {
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));
  EXPECT_CALL(mock_filter_callback_, completeDirectly());

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionErrorBeforeResponse) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "local_reset");
      }));

  mock_conn.close(Network::ConnectionCloseType::FlushWrite);
  // Mock connection close event.
  upstream_request->onEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionTerminationBeforeResponse) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "connection_termination");
      }));

  mock_conn.close(Network::ConnectionCloseType::FlushWrite);
  // Mock connection close event.
  upstream_request->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButStreamDestroyBeforeResponse) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  EXPECT_CALL(mock_conn, close(Network::ConnectionCloseType::NoFlush));

  filter_->onDestroy();
  EXPECT_EQ(upstream_request->conn_data_, nullptr);
  // Do nothing for the second call.
  filter_->onDestroy();
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponse) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  auto mock_response_decoder = std::make_unique<NiceMock<MockResponseDecoder>>();
  auto raw_mock_response_decoder = mock_response_decoder.get();

  EXPECT_CALL(mock_filter_callback_, downstreamCodec()).WillOnce(ReturnRef(mock_codec_factory_));
  EXPECT_CALL(mock_codec_factory_, responseDecoder())
      .WillOnce(Return(ByMove(std::move(mock_response_decoder))));
  EXPECT_CALL(*raw_mock_response_decoder, setDecoderCallback(_))
      .WillOnce(Invoke([&](ResponseDecoderCallback& cb) { EXPECT_EQ(upstream_request, &cb); }));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test_1");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_1")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  upstream_request->onUpstreamData(test_buffer, false);

  EXPECT_EQ(0, test_buffer.length());

  test_buffer.add("test_2");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_2")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  upstream_request->onUpstreamData(test_buffer, false);

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();

  EXPECT_CALL(mock_filter_callback_, upstreamResponse(_));

  upstream_request->onDecodingSuccess(std::move(response));
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndEndStreamBeforeResponse) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  auto mock_response_decoder = std::make_unique<NiceMock<MockResponseDecoder>>();
  auto raw_mock_response_decoder = mock_response_decoder.get();

  EXPECT_CALL(mock_filter_callback_, downstreamCodec()).WillOnce(ReturnRef(mock_codec_factory_));
  EXPECT_CALL(mock_codec_factory_, responseDecoder())
      .WillOnce(Return(ByMove(std::move(mock_response_decoder))));
  EXPECT_CALL(*raw_mock_response_decoder, setDecoderCallback(_))
      .WillOnce(Invoke([&](ResponseDecoderCallback& cb) { EXPECT_EQ(upstream_request, &cb); }));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test_1");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_1")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  upstream_request->onUpstreamData(test_buffer, false);

  EXPECT_EQ(0, test_buffer.length());

  test_buffer.add("test_2");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_2")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "protocol_error");
      }));

  upstream_request->onUpstreamData(test_buffer, true);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseDecodingFailure) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  auto mock_response_decoder = std::make_unique<NiceMock<MockResponseDecoder>>();
  auto raw_mock_response_decoder = mock_response_decoder.get();

  EXPECT_CALL(mock_filter_callback_, downstreamCodec()).WillOnce(ReturnRef(mock_codec_factory_));
  EXPECT_CALL(mock_codec_factory_, responseDecoder())
      .WillOnce(Return(ByMove(std::move(mock_response_decoder))));
  EXPECT_CALL(*raw_mock_response_decoder, setDecoderCallback(_))
      .WillOnce(Invoke([&](ResponseDecoderCallback& cb) { EXPECT_EQ(upstream_request, &cb); }));

  Buffer::OwnedImpl test_buffer;
  test_buffer.add("test_1");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_1")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  upstream_request->onUpstreamData(test_buffer, false);

  EXPECT_EQ(0, test_buffer.length());

  test_buffer.add("test_2");

  EXPECT_CALL(*raw_mock_response_decoder, decode(BufferStringEqual("test_2")))
      .WillOnce(Invoke([&](Buffer::Instance& buffer) { buffer.drain(buffer.length()); }));

  upstream_request->onUpstreamData(test_buffer, false);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "protocol_error");
      }));

  upstream_request->onDecodingFailure();
}

} // namespace
} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
