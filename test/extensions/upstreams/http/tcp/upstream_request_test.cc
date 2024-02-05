#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/router.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/router/upstream_request.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/tcp/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::TestRequestHeaderMapImpl;
using Envoy::Router::UpstreamRequest;
using testing::_;
using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

class TcpConnPoolTest : public ::testing::Test {
public:
  TcpConnPoolTest() : host_(std::make_shared<NiceMock<Upstream::MockHost>>()) {
    Upstream::ResourcePriority priority = Upstream::ResourcePriority::Default;
    NiceMock<Upstream::MockClusterManager> cm;
    cm.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm.thread_local_cluster_, tcpConnPool(_, _))
        .WillOnce(Return(Upstream::TcpPoolData([]() {}, &mock_pool_)));
    conn_pool_ = std::make_unique<TcpConnPool>(cm.thread_local_cluster_, priority, nullptr);
  }

  std::unique_ptr<TcpConnPool> conn_pool_;
  Envoy::Tcp::ConnectionPool::MockInstance mock_pool_;
  Router::MockGenericConnectionPoolCallbacks mock_generic_callbacks_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  NiceMock<Envoy::ConnectionPool::MockCancellable> cancellable_;
};

TEST_F(TcpConnPoolTest, Basic) {
  NiceMock<Network::MockClientConnection> connection;

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_generic_callbacks_);

  EXPECT_CALL(mock_generic_callbacks_, upstreamToDownstream());
  EXPECT_CALL(mock_generic_callbacks_, onPoolReady(_, _, _, _, _));
  auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*data, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection));
  conn_pool_->onPoolReady(std::move(data), host_);
}

TEST_F(TcpConnPoolTest, OnPoolFailure) {
  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_generic_callbacks_);

  EXPECT_CALL(mock_generic_callbacks_, onPoolFailure(_, "foo", _));
  conn_pool_->onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                            "foo", host_);

  // Make sure that the pool failure nulled out the pending request.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

TEST_F(TcpConnPoolTest, Cancel) {
  // Initially cancel should fail as there is no pending request.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_generic_callbacks_);

  // Canceling should now return true as there was an active request.
  EXPECT_TRUE(conn_pool_->cancelAnyPendingStream());

  // A second cancel should return false as there is not a pending request.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

class TcpUpstreamTest : public ::testing::Test {
public:
  TcpUpstreamTest() {
    ON_CALL(*mock_router_filter_.cluster_info_, createFilterChain(_, _, _))
        .WillByDefault(
            Invoke([&](Envoy::Http::FilterChainManager& manager, bool only_create_if_configured,
                       const Envoy::Http::FilterChainOptions&) -> bool {
              if (only_create_if_configured) {
                return false;
              }
              Envoy ::Http::FilterFactoryCb factory_cb =
                  [](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
                callbacks.addStreamDecoderFilter(std::make_shared<Router::UpstreamCodecFilter>());
              };
              manager.applyFilterFactoryCb({}, factory_cb);
              return true;
            }));
    EXPECT_CALL(mock_router_filter_, downstreamHeaders())
        .Times(AnyNumber())
        .WillRepeatedly(Return(&request_));
    EXPECT_CALL(mock_router_filter_, cluster()).Times(AnyNumber());
    EXPECT_CALL(mock_router_filter_, callbacks()).Times(AnyNumber());
    upstream_request_ = std::make_unique<UpstreamRequest>(
        mock_router_filter_, std::make_unique<NiceMock<Router::MockGenericConnPool>>(), false,
        false, false /*enable_tcp_tunneling*/);
    auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
    EXPECT_CALL(*data, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection()));
    tcp_upstream_ = std::make_unique<TcpUpstream>(upstream_request_.get(), std::move(data));
  }
  ~TcpUpstreamTest() override { EXPECT_CALL(mock_router_filter_, config()).Times(AnyNumber()); }
  NiceMock<Network::MockClientConnection>& connection() {
    return mock_router_filter_.client_connection_;
  }

protected:
  TestRequestHeaderMapImpl request_{{":method", "CONNECT"},
                                    {":path", "/"},
                                    {":protocol", "bytestream"},
                                    {":scheme", "https"},
                                    {":authority", "host"}};
  NiceMock<Router::MockRouterFilterInterface> mock_router_filter_;
  std::unique_ptr<UpstreamRequest> upstream_request_;
  std::unique_ptr<TcpUpstream> tcp_upstream_;
};

TEST_F(TcpUpstreamTest, Basic) {
  // Swallow the request headers and generate response headers.
  EXPECT_CALL(connection(), write(_, false)).Times(0);
  EXPECT_CALL(mock_router_filter_, onUpstreamHeaders(200, _, _, false));
  EXPECT_TRUE(tcp_upstream_->encodeHeaders(request_, false).ok());

  // Proxy the data.
  EXPECT_CALL(connection(), write(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer("foo");
  tcp_upstream_->encodeData(buffer, false);

  // Metadata is swallowed.
  Envoy::Http::MetadataMapVector metadata_map_vector;
  tcp_upstream_->encodeMetadata(metadata_map_vector);

  // Forward data.
  Buffer::OwnedImpl response1("bar");
  EXPECT_CALL(mock_router_filter_, onUpstreamData(BufferStringEqual("bar"), _, false));
  tcp_upstream_->onUpstreamData(response1, false);

  Buffer::OwnedImpl response2("eep");
  EXPECT_CALL(mock_router_filter_, onUpstreamHeaders(_, _, _, _)).Times(0);
  EXPECT_CALL(mock_router_filter_, onUpstreamData(BufferStringEqual("eep"), _, false));
  tcp_upstream_->onUpstreamData(response2, false);
}

TEST_F(TcpUpstreamTest, V1Header) {
  envoy::config::core::v3::ProxyProtocolConfig* proxy_config =
      mock_router_filter_.callbacks_.route_->route_entry_.connect_config_
          ->mutable_proxy_protocol_config();
  proxy_config->set_version(envoy::config::core::v3::ProxyProtocolConfig::V1);
  mock_router_filter_.client_connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 5));
  mock_router_filter_.client_connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(std::make_shared<Network::Address::Ipv4Instance>("4.5.6.7", 8));

  Buffer::OwnedImpl expected_data;
  Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
      *proxy_config, mock_router_filter_.client_connection_, expected_data);

  // encodeHeaders now results in the proxy proto header being sent.
  EXPECT_CALL(connection(), write(BufferEqual(&expected_data), false));
  EXPECT_TRUE(tcp_upstream_->encodeHeaders(request_, false).ok());

  // Data is proxied as usual.
  EXPECT_CALL(connection(), write(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer("foo");
  tcp_upstream_->encodeData(buffer, false);
}

TEST_F(TcpUpstreamTest, V2Header) {
  envoy::config::core::v3::ProxyProtocolConfig* proxy_config =
      mock_router_filter_.callbacks_.route_->route_entry_.connect_config_
          ->mutable_proxy_protocol_config();
  proxy_config->set_version(envoy::config::core::v3::ProxyProtocolConfig::V2);
  mock_router_filter_.client_connection_.stream_info_.downstream_connection_info_provider_
      ->setRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 5));
  mock_router_filter_.client_connection_.stream_info_.downstream_connection_info_provider_
      ->setLocalAddress(std::make_shared<Network::Address::Ipv4Instance>("4.5.6.7", 8));

  Buffer::OwnedImpl expected_data;
  Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
      *proxy_config, mock_router_filter_.client_connection_, expected_data);

  // encodeHeaders now results in the proxy proto header being sent.
  EXPECT_CALL(connection(), write(BufferEqual(&expected_data), false));
  EXPECT_TRUE(tcp_upstream_->encodeHeaders(request_, false).ok());

  // Data is proxied as usual.
  EXPECT_CALL(connection(), write(BufferStringEqual("foo"), false));
  Buffer::OwnedImpl buffer("foo");
  tcp_upstream_->encodeData(buffer, false);
}

TEST_F(TcpUpstreamTest, TrailersEndStream) {
  // Swallow the headers.
  EXPECT_TRUE(tcp_upstream_->encodeHeaders(request_, false).ok());

  EXPECT_CALL(connection(), write(BufferStringEqual(""), true));
  Envoy::Http::TestRequestTrailerMapImpl trailers{{"foo", "bar"}};
  tcp_upstream_->encodeTrailers(trailers);
}

TEST_F(TcpUpstreamTest, HeaderEndStreamHalfClose) {
  EXPECT_CALL(connection(), write(BufferStringEqual(""), true));
  EXPECT_TRUE(tcp_upstream_->encodeHeaders(request_, true).ok());
}

TEST_F(TcpUpstreamTest, ReadDisable) {
  EXPECT_CALL(connection(), readDisable(true));
  tcp_upstream_->readDisable(true);

  EXPECT_CALL(connection(), readDisable(false));
  tcp_upstream_->readDisable(false);

  // Once the connection is closed, don't touch it.
  connection().state_ = Network::Connection::State::Closed;
  EXPECT_CALL(connection(), readDisable(_)).Times(0);
  tcp_upstream_->readDisable(true);
}

TEST_F(TcpUpstreamTest, UpstreamEvent) {
  // Make sure upstream disconnects result in stream reset.
  EXPECT_CALL(mock_router_filter_,
              onUpstreamReset(Envoy::Http::StreamResetReason::ConnectionTermination, "", _));
  tcp_upstream_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(TcpUpstreamTest, Watermarks) {
  EXPECT_CALL(mock_router_filter_, callbacks()).Times(AnyNumber());
  EXPECT_CALL(mock_router_filter_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  tcp_upstream_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_router_filter_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  tcp_upstream_->onBelowWriteBufferLowWatermark();
}

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
