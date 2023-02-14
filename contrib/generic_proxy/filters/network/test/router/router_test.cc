#include "source/common/tracing/common_values.h"

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
    request_ = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

    // Common mock calls.
    ON_CALL(mock_filter_callback_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(mock_filter_callback_, activeSpan()).WillByDefault(ReturnRef(active_span_));
    ON_CALL(mock_filter_callback_, downstreamCodec()).WillByDefault(ReturnRef(mock_codec_factory_));
    ON_CALL(mock_filter_callback_, streamInfo()).WillByDefault(ReturnRef(mock_stream_info_));
  }

  void kickOffNewUpstreamRequest(bool with_tracing) {
    EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

    const std::string cluster_name = "cluster_0";

    EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));
    factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

    EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
                newConnection(_));

    if (with_tracing) {
      EXPECT_CALL(mock_filter_callback_, tracingConfig())
          .WillOnce(Return(OptRef<const Tracing::Config>{tracing_config_}));
      EXPECT_CALL(active_span_, spawnChild_(_, "router observability_name egress", _))
          .WillOnce(
              Invoke([&](const Tracing::Config&, const std::string&, SystemTime) -> Tracing::Span* {
                child_span_ = new NiceMock<Tracing::MockSpan>();
                return child_span_;
              }));
    } else {
      EXPECT_CALL(mock_filter_callback_, tracingConfig())
          .WillOnce(Return(OptRef<const Tracing::Config>{}));
    }

    auto request_encoder = std::make_unique<NiceMock<MockRequestEncoder>>();
    mock_request_encoder_ = request_encoder.get();
    EXPECT_CALL(mock_codec_factory_, requestEncoder())
        .WillOnce(Return(testing::ByMove(std::move(request_encoder))));

    EXPECT_EQ(filter_->onStreamDecoded(*request_), FilterStatus::StopIteration);
    EXPECT_EQ(1, filter_->upstreamRequestsForTest().size());
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Envoy::Event::MockDispatcher> dispatcher_;

  NiceMock<MockDecoderFilterCallback> mock_filter_callback_;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;

  NiceMock<MockCodecFactory> mock_codec_factory_;
  NiceMock<MockRequestEncoder>* mock_request_encoder_{};

  NiceMock<MockRouteEntry> mock_route_entry_;

  std::shared_ptr<Router::RouterFilter> filter_;
  std::unique_ptr<FakeStreamCodecFactory::FakeRequest> request_;

  NiceMock<Tracing::MockSpan> active_span_;
  NiceMock<Tracing::MockSpan>* child_span_{};
  NiceMock<Tracing::MockConfig> tracing_config_;
};

TEST_F(RouterFilterTest, OnStreamDecodedAndNoRouteEntry) {
  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "route_not_found");
      }));

  EXPECT_EQ(filter_->onStreamDecoded(*request_), FilterStatus::StopIteration);
}

TEST_F(RouterFilterTest, OnStreamDecodedWithRouteEntry) { kickOffNewUpstreamRequest(false); }

TEST_F(RouterFilterTest, NoUpstreamCluster) {
  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  // No upstream cluster.
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "cluster_not_found");
      }));

  filter_->onStreamDecoded(*request_);
}

TEST_F(RouterFilterTest, UpstreamClusterMaintainMode) {
  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

  // Maintain mode.
  EXPECT_CALL(*factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
              maintenanceMode())
      .WillOnce(Return(true));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "cluster_maintain_mode");
      }));

  filter_->onStreamDecoded(*request_);
}

TEST_F(RouterFilterTest, UpstreamClusterNoHealthyUpstream) {
  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  factory_context_.cluster_manager_.initializeThreadLocalClusters({cluster_name});

  // No conn pool.
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(status.message(), "no_healthy_upstream");
      }));

  filter_->onStreamDecoded(*request_);
}

TEST_F(RouterFilterTest, KickOffNormalUpstreamRequestAndWithTracing) {
  kickOffNewUpstreamRequest(true);
}

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
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "local_reset");
      }));

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  filter_->upstreamRequestsForTest().begin()->get()->resetStream(StreamResetReason::LocalReset);

  EXPECT_EQ(nullptr, upstream_request->conn_pool_handle_);
}

TEST_F(RouterFilterTest, UpstreamRequestResetBeforePoolCallbackWithTracing) {
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "local_reset"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
  EXPECT_CALL(*child_span_, finishSpan());

  EXPECT_CALL(
      factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.handles_.back(),
      cancel(_));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
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

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionOverflowWithTracing) {
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "overflow"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
  EXPECT_CALL(*child_span_, finishSpan());

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

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeoutWithTracing) {
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "connection_failure"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
  EXPECT_CALL(*child_span_, finishSpan());

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

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect no response.
        callback.onEncodingSuccess(buffer, false);
      }));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndExpectNoResponseWithTracing) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));
  EXPECT_CALL(mock_filter_callback_, completeDirectly()).WillOnce(Invoke([this]() -> void {
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect no response.
        callback.onEncodingSuccess(buffer, false);
      }));

  // Request complete directly.
  EXPECT_CALL(*child_span_, injectContext(_, _));
  EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*child_span_, finishSpan());

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionErrorBeforeResponse) {
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "local_reset");
      }));

  mock_conn.close(Network::ConnectionCloseType::FlushWrite);
  // Mock connection close event.
  upstream_request->onEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionTerminationBeforeResponse) {
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(mock_conn);

  EXPECT_NE(upstream_request->conn_data_, nullptr);
  EXPECT_EQ(upstream_request->conn_pool_handle_, nullptr);

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _))
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "connection_termination");
      }));

  mock_conn.close(Network::ConnectionCloseType::FlushWrite);
  // Mock connection close event.
  upstream_request->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(upstream_request->conn_data_, nullptr);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButStreamDestroyBeforeResponse) {
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

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
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

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

  EXPECT_CALL(mock_filter_callback_, upstreamResponse(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  upstream_request->onDecodingSuccess(std::move(response));
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithTracing) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));
  // Inject tracing context.
  EXPECT_CALL(*child_span_, injectContext(_, _));

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

  EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*child_span_, finishSpan());

  EXPECT_CALL(mock_filter_callback_, upstreamResponse(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  upstream_request->onDecodingSuccess(std::move(response));
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndEndStreamBeforeResponseWithTracing) {
  kickOffNewUpstreamRequest(true);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));
  // Inject tracing context.
  EXPECT_CALL(*child_span_, injectContext(_, _));

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
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "protocol_error");
      }));

  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "protocol_error"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
  EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
  EXPECT_CALL(*child_span_, finishSpan());

  upstream_request->onUpstreamData(test_buffer, true);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseDecodingFailure) {
  kickOffNewUpstreamRequest(false);

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  NiceMock<Network::MockClientConnection> mock_conn;

  EXPECT_CALL(mock_conn, write(_, _));

  EXPECT_CALL(*mock_request_encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Request&, RequestEncoderCallback& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

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
      .WillOnce(Invoke([this](Status status, ResponseUpdateFunction&&) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
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
