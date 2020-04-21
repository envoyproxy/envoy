#include "common/buffer/buffer_impl.h"
#include "common/router/config_impl.h"
#include "common/router/router.h"
#include "common/router/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

class MockGenericConnPool : public GenericConnPool {
  MOCK_METHOD(void, newStream, (GenericConnectionPoolCallbacks * request));
  MOCK_METHOD(bool, cancelAnyPendingRequest, ());
  MOCK_METHOD(absl::optional<Http::Protocol>, protocol, (), (const));
};

class MockGenericConnectionPoolCallbacks : public GenericConnectionPoolCallbacks {
public:
  MOCK_METHOD(void, onPoolFailure,
              (Http::ConnectionPool::PoolFailureReason reason,
               absl::string_view transport_failure_reason,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPoolReady,
              (std::unique_ptr<GenericUpstream> && upstream,
               Upstream::HostDescriptionConstSharedPtr host,
               const Network::Address::InstanceConstSharedPtr& upstream_local_address,
               const StreamInfo::StreamInfo& info));
  MOCK_METHOD(UpstreamRequest*, upstreamRequest, ());
};

class MockRouterFilterInterface : public RouterFilterInterface {
public:
  MockRouterFilterInterface()
      : config_("prefix.", context_, ShadowWriterPtr(new MockShadowWriter()), router_proto) {
    auto cluster_info = new NiceMock<Upstream::MockClusterInfo>();
    cluster_info->timeout_budget_stats_ = absl::nullopt;
    cluster_info_.reset(cluster_info);
    ON_CALL(*this, callbacks()).WillByDefault(Return(&callbacks_));
    ON_CALL(*this, config()).WillByDefault(ReturnRef(config_));
    ON_CALL(*this, cluster()).WillByDefault(Return(cluster_info_));
    ON_CALL(*this, upstreamRequests()).WillByDefault(ReturnRef(requests_));
    EXPECT_CALL(callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
  }

  MOCK_METHOD(void, onUpstream100ContinueHeaders,
              (Http::ResponseHeaderMapPtr && headers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHeaders,
              (uint64_t response_code, Http::ResponseHeaderMapPtr&& headers,
               UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamData,
              (Buffer::Instance & data, UpstreamRequest& upstream_request, bool end_stream));
  MOCK_METHOD(void, onUpstreamTrailers,
              (Http::ResponseTrailerMapPtr && trailers, UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamMetadata, (Http::MetadataMapPtr && metadata_map));
  MOCK_METHOD(void, onUpstreamReset,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure,
               UpstreamRequest& upstream_request));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onPerTryTimeout, (UpstreamRequest & upstream_request));

  MOCK_METHOD(Http::StreamDecoderFilterCallbacks*, callbacks, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, cluster, ());
  MOCK_METHOD(FilterConfig&, config, ());
  MOCK_METHOD(FilterUtility::TimeoutData, timeout, ());
  MOCK_METHOD(Http::RequestHeaderMap*, downstreamHeaders, ());
  MOCK_METHOD(Http::RequestTrailerMap*, downstreamTrailers, ());
  MOCK_METHOD(bool, downstreamResponseStarted, (), (const));
  MOCK_METHOD(bool, downstreamEndStream, (), (const));
  MOCK_METHOD(uint32_t, attemptCount, (), (const));
  MOCK_METHOD(const VirtualCluster*, requestVcluster, (), (const));
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const std::list<UpstreamRequestPtr>&, upstreamRequests, (), (const));
  MOCK_METHOD(const UpstreamRequest*, finalUpstreamRequest, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;

  envoy::extensions::filters::http::router::v3::Router router_proto;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  FilterConfig config_;
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  std::list<UpstreamRequestPtr> requests_;
};

class TcpConnPoolTest : public ::testing::Test {
public:
  TcpConnPoolTest()
      : conn_pool_(&mock_pool_), host_(std::make_shared<NiceMock<Upstream::MockHost>>()) {}

  TcpConnPool conn_pool_;
  Tcp::ConnectionPool::MockInstance mock_pool_;
  MockGenericConnectionPoolCallbacks mock_generic_callbacks_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  NiceMock<Tcp::ConnectionPool::MockCancellable> cancellable_;
};

TEST_F(TcpConnPoolTest, Basic) {
  NiceMock<Network::MockClientConnection> connection;

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_.newStream(&mock_generic_callbacks_);

  EXPECT_CALL(mock_generic_callbacks_, upstreamRequest());
  EXPECT_CALL(mock_generic_callbacks_, onPoolReady(_, _, _, _));
  auto data = std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*data, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection));
  conn_pool_.onPoolReady(std::move(data), host_);
}

TEST_F(TcpConnPoolTest, OnPoolFailure) {
  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_.newStream(&mock_generic_callbacks_);

  EXPECT_CALL(mock_generic_callbacks_, onPoolFailure(_, _, _));
  conn_pool_.onPoolFailure(Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure, host_);

  // Make sure that the pool failure nulled out the pending request.
  EXPECT_FALSE(conn_pool_.cancelAnyPendingRequest());
}

TEST_F(TcpConnPoolTest, Cancel) {
  // Initially cancel should fail as there is no pending request.
  EXPECT_FALSE(conn_pool_.cancelAnyPendingRequest());

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_.newStream(&mock_generic_callbacks_);

  // Canceling should now return true as there was an active request.
  EXPECT_TRUE(conn_pool_.cancelAnyPendingRequest());

  // A second cancel should return false as there is not a pending request.
  EXPECT_FALSE(conn_pool_.cancelAnyPendingRequest());
}

class TcpUpstreamTest : public ::testing::Test {
public:
  TcpUpstreamTest() {
    mock_router_filter_.requests_.push_back(std::make_unique<UpstreamRequest>(
        mock_router_filter_, std::make_unique<NiceMock<MockGenericConnPool>>()));
    auto data = std::make_unique<NiceMock<Tcp::ConnectionPool::MockConnectionData>>();
    EXPECT_CALL(*data, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection_));
    tcp_upstream_ =
        std::make_unique<TcpUpstream>(mock_router_filter_.requests_.front().get(), std::move(data));
  }
  ~TcpUpstreamTest() override { EXPECT_CALL(mock_router_filter_, config()).Times(AnyNumber()); }

protected:
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<MockRouterFilterInterface> mock_router_filter_;
  Tcp::ConnectionPool::MockConnectionData* mock_connection_data_;
  std::unique_ptr<TcpUpstream> tcp_upstream_;
  Http::TestRequestHeaderMapImpl request_{{":method", "CONNECT"},
                                          {":path", "/"},
                                          {":protocol", "bytestream"},
                                          {":scheme", "https"},
                                          {":authority", "host"}};
};

TEST_F(TcpUpstreamTest, Basic) {
  // Swallow the headers.
  tcp_upstream_->encodeHeaders(request_, false);

  // Proxy the data.
  EXPECT_CALL(connection_, write(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer("foo");
  tcp_upstream_->encodeData(buffer, false);

  // Metadata is swallowed.
  Http::MetadataMapVector metadata_map_vector;
  tcp_upstream_->encodeMetadata(metadata_map_vector);

  // On initial data payload, fake response headers, and forward data.
  Buffer::OwnedImpl response1("bar");
  EXPECT_CALL(mock_router_filter_, onUpstreamHeaders(200, _, _, false));
  EXPECT_CALL(mock_router_filter_, onUpstreamData(BufferStringEqual("bar"), _, false));
  tcp_upstream_->onUpstreamData(response1, false);

  // On the next batch of payload there won't be additional headers.
  Buffer::OwnedImpl response2("eep");
  EXPECT_CALL(mock_router_filter_, onUpstreamHeaders(_, _, _, _)).Times(0);
  EXPECT_CALL(mock_router_filter_, onUpstreamData(BufferStringEqual("eep"), _, false));
  tcp_upstream_->onUpstreamData(response2, false);
}

TEST_F(TcpUpstreamTest, TrailersEndStream) {
  // Swallow the headers.
  tcp_upstream_->encodeHeaders(request_, false);

  EXPECT_CALL(connection_, write(BufferStringEqual(""), true));
  Http::TestRequestTrailerMapImpl trailers{{"foo", "bar"}};
  tcp_upstream_->encodeTrailers(trailers);
}

TEST_F(TcpUpstreamTest, HeaderEndStreamHalfClose) {
  EXPECT_CALL(connection_, write(BufferStringEqual(""), true));
  tcp_upstream_->encodeHeaders(request_, true);
}

TEST_F(TcpUpstreamTest, ReadDisable) {
  EXPECT_CALL(connection_, readDisable(true));
  tcp_upstream_->readDisable(true);

  EXPECT_CALL(connection_, readDisable(false));
  tcp_upstream_->readDisable(false);

  // Once the connection is closed, don't touch it.
  connection_.state_ = Network::Connection::State::Closed;
  EXPECT_CALL(connection_, readDisable(_)).Times(0);
  tcp_upstream_->readDisable(true);
}

TEST_F(TcpUpstreamTest, UpstreamEvent) {
  // Make sure upstream disconnects result in stream reset.
  EXPECT_CALL(mock_router_filter_,
              onUpstreamReset(Http::StreamResetReason::ConnectionTermination, "", _));
  tcp_upstream_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpUpstreamTest, Watermarks) {
  EXPECT_CALL(mock_router_filter_, callbacks()).Times(AnyNumber());
  EXPECT_CALL(mock_router_filter_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  tcp_upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_router_filter_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  tcp_upstream_->onBelowWriteBufferLowWatermark();
}

} // namespace
} // namespace Router
} // namespace Envoy
