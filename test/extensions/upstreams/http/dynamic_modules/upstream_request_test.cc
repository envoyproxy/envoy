#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/upstreams/http/dynamic_modules/config.h"
#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {
namespace {

void setTestModulesSearchPath() {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);
}

BridgeConfigSharedPtr createBridgeConfig(const std::string& module_name) {
  auto module =
      Envoy::Extensions::DynamicModules::newDynamicModuleByName(module_name, false, false);
  EXPECT_TRUE(module.ok());
  auto config_or = BridgeConfig::create("test_bridge", "", std::move(module.value()));
  EXPECT_TRUE(config_or.ok());
  return config_or.value();
}

// =============================================================================
// BridgeConfig tests.
// =============================================================================

class BridgeConfigTest : public ::testing::Test {
public:
  BridgeConfigTest() { setTestModulesSearchPath(); }
};

TEST_F(BridgeConfigTest, CreateSuccess) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName("upstream_bridge_no_op",
                                                                          false, false);
  ASSERT_TRUE(module.ok());

  auto config = BridgeConfig::create("test_bridge", "test_config", std::move(module.value()));
  ASSERT_TRUE(config.ok());
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST_F(BridgeConfigTest, CreateFailConfigNewReturnsNull) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      "upstream_bridge_config_new_fail", false, false);
  ASSERT_TRUE(module.ok());

  auto config = BridgeConfig::create("test_bridge", "test_config", std::move(module.value()));
  ASSERT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("failed to initialize dynamic module bridge configuration"));
}

// =============================================================================
// TcpConnPool tests.
// =============================================================================

class TcpConnPoolTest : public ::testing::Test {
public:
  TcpConnPoolTest() : host_(std::make_shared<NiceMock<Upstream::MockHost>>()) {
    setTestModulesSearchPath();
    bridge_config_ = createBridgeConfig("upstream_bridge_no_op");

    NiceMock<Upstream::MockClusterManager> cm;
    cm.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm.thread_local_cluster_, tcpConnPool(_, _, _))
        .WillOnce(Return(Upstream::TcpPoolData([]() {}, &mock_pool_)));
    conn_pool_ =
        std::make_unique<TcpConnPool>(nullptr, cm.thread_local_cluster_,
                                      Upstream::ResourcePriority::Default, nullptr, bridge_config_);
  }

  BridgeConfigSharedPtr bridge_config_;
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

  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

TEST_F(TcpConnPoolTest, Cancel) {
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_generic_callbacks_);

  EXPECT_TRUE(conn_pool_->cancelAnyPendingStream());
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

// =============================================================================
// HttpTcpBridge tests with the no-op module.
// =============================================================================

class HttpTcpBridgeTest : public ::testing::Test {
public:
  void createBridge(const std::string& module_name) {
    setTestModulesSearchPath();
    bridge_config_ = createBridgeConfig(module_name);

    auto conn_data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
    mock_conn_data_ = conn_data.get();
    EXPECT_CALL(*mock_conn_data_, connection())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(mock_connection_));

    bridge_ = std::make_unique<HttpTcpBridge>(&mock_upstream_to_downstream_, std::move(conn_data),
                                              bridge_config_);
  }

  HttpTcpBridgeTest() { createBridge("upstream_bridge_no_op"); }

  BridgeConfigSharedPtr bridge_config_;
  NiceMock<Network::MockClientConnection> mock_connection_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>* mock_conn_data_;
  NiceMock<Router::MockUpstreamToDownstream> mock_upstream_to_downstream_;
  std::unique_ptr<HttpTcpBridge> bridge_;
};

TEST_F(HttpTcpBridgeTest, EncodeHeadersStreamingMode) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {":authority", "example.com"}};

  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeTest, EncodeHeadersEndOfStream) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};

  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeTest, EncodeData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl data("hello");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeTest, EncodeTrailers) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer-key", "trailer-value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeTest, OnUpstreamData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  bridge_->onUpstreamData(data, false);
  // Buffer should be drained after Continue.
  EXPECT_EQ(0, data.length());
}

TEST_F(HttpTcpBridgeTest, OnUpstreamConnectionClose) {
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _));
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpTcpBridgeTest, ResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();
}

TEST_F(HttpTcpBridgeTest, ReadDisable) {
  EXPECT_CALL(mock_connection_, state()).WillOnce(Return(Network::Connection::State::Open));
  EXPECT_CALL(mock_connection_, readDisable(true));
  bridge_->readDisable(true);
}

TEST_F(HttpTcpBridgeTest, Watermarks) {
  EXPECT_CALL(mock_upstream_to_downstream_, onAboveWriteBufferHighWatermark());
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_upstream_to_downstream_, onBelowWriteBufferLowWatermark());
  bridge_->onBelowWriteBufferLowWatermark();
}

// =============================================================================
// HttpTcpBridge tests for null bridge (bridge_new returns nullptr).
// =============================================================================

class HttpTcpBridgeNullBridgeTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeNullBridgeTest() { createBridge("upstream_bridge_new_fail"); }
};

TEST_F(HttpTcpBridgeNullBridgeTest, EncodeHeadersReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "dynamic module bridge is null");
}

TEST_F(HttpTcpBridgeNullBridgeTest, EncodeDataIsNoOp) {
  Buffer::OwnedImpl data("hello");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeNullBridgeTest, EncodeTrailersIsNoOp) {
  Envoy::Http::TestRequestTrailerMapImpl trailers{{"key", "value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeNullBridgeTest, OnUpstreamDataIsNoOp) {
  Buffer::OwnedImpl data("response");
  bridge_->onUpstreamData(data, false);
}

// =============================================================================
// HttpTcpBridge tests for StopAndBuffer behavior.
// =============================================================================

class HttpTcpBridgeStopAndBufferTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeStopAndBufferTest() { createBridge("upstream_bridge_stop_and_buffer"); }
};

TEST_F(HttpTcpBridgeStopAndBufferTest, EncodeHeadersStopAndBuffer) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeStopAndBufferTest, EncodeDataBufferedModeAccumulatesAndSendsOnEndStream) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // In buffered mode, data is accumulated. Non-end-of-stream chunks are buffered without
  // sending to upstream.
  Buffer::OwnedImpl chunk1("chunk1");
  bridge_->encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2("chunk2");
  bridge_->encodeData(chunk2, false);

  // On end_of_stream, the module's encode_data callback is invoked and the accumulated
  // buffer is sent upstream.
  Buffer::OwnedImpl chunk3("chunk3");
  bridge_->encodeData(chunk3, true);
}

TEST_F(HttpTcpBridgeStopAndBufferTest, OnUpstreamDataStopAndBufferDoesNotDrain) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl data("upstream data");
  bridge_->onUpstreamData(data, false);
  // StopAndBuffer should not drain the buffer.
  EXPECT_EQ(13, data.length());
}

// =============================================================================
// HttpTcpBridge tests for EndStream behavior.
// =============================================================================

class HttpTcpBridgeEndStreamTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeEndStreamTest() { createBridge("upstream_bridge_end_stream"); }
};

TEST_F(HttpTcpBridgeEndStreamTest, EncodeDataEndStreamSendsLocalReply) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // The end_stream module sets response body to "end_stream_body" and status to 403 in
  // encode_data, then returns EndStream which triggers sendLocalReply.
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Buffer::OwnedImpl data("request data");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeEndStreamTest, EncodeTrailersEndStreamSendsLocalReply) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // The end_stream module returns EndStream from encode_trailers, triggering sendLocalReply.
  // No body is set in encode_trailers, so headers are sent with end_stream=true.
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"key", "value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeEndStreamTest, OnUpstreamDataEndStreamSendsResponse) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // The end_stream module sets response trailers during encode_headers, so
  // sendResponseToDownstream sends headers, then empty data (end_stream=false),
  // then trailers to close the stream.
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeTrailers(_));

  Buffer::OwnedImpl data("upstream response");
  bridge_->onUpstreamData(data, false);
  // Buffer should be drained after EndStream.
  EXPECT_EQ(0, data.length());
}

TEST_F(HttpTcpBridgeEndStreamTest, AbiCallbacksExercised) {
  // The end_stream module exercises various ABI callbacks during encode_headers:
  // get_request_headers_size, get_request_headers, get_request_buffer, set_request_buffer,
  // drain_request_buffer, set_response_trailer, add_response_trailer.
  // After the module callback, sendDataToUpstream writes the request buffer to the upstream
  // connection, so we verify via the connection write call.
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {"x-custom", "value"}};

  Buffer::OwnedImpl captured_data;
  EXPECT_CALL(mock_connection_, write(_, false))
      .WillOnce(
          Invoke([&captured_data](Buffer::Instance& data, bool) { captured_data.move(data); }));

  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());

  // The module called set_request_buffer("replaced") then drain(3), leaving "laced".
  // sendDataToUpstream then wrote this to the connection.
  EXPECT_EQ("laced", captured_data.toString());

  // Verify response trailers were set by the module.
  EXPECT_NE(bridge_->responseTrailers(), nullptr);
}

// =============================================================================
// HttpTcpBridge tests for encodeHeaders EndStream (deferred local reply).
// =============================================================================

class HttpTcpBridgeHeadersEndStreamTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeHeadersEndStreamTest() { createBridge("upstream_bridge_headers_end_stream"); }
};

TEST_F(HttpTcpBridgeHeadersEndStreamTest, DeferredLocalReplyWithBody) {
  Event::PostCb captured_cb;
  EXPECT_CALL(mock_connection_.dispatcher_, post(_))
      .WillOnce(Invoke([&captured_cb](Event::PostCb cb) { captured_cb = std::move(cb); }));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());

  // The callback should have been posted. Now invoke it to simulate the next event loop tick.
  ASSERT_NE(captured_cb, nullptr);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));
  captured_cb();
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, DeferredLocalReplyGuardInvalidated) {
  Event::PostCb captured_cb;
  EXPECT_CALL(mock_connection_.dispatcher_, post(_))
      .WillOnce(Invoke([&captured_cb](Event::PostCb cb) { captured_cb = std::move(cb); }));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());

  // Destroy the bridge before the callback runs.
  bridge_.reset();

  // The guard should prevent the callback from doing anything.
  ASSERT_NE(captured_cb, nullptr);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);
  captured_cb();
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, DeferredLocalReplyWithoutBody) {
  createBridge("upstream_bridge_headers_end_stream_no_body");

  Event::PostCb captured_cb;
  EXPECT_CALL(mock_connection_.dispatcher_, post(_))
      .WillOnce(Invoke([&captured_cb](Event::PostCb cb) { captured_cb = std::move(cb); }));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());

  // The module did not set any response body, so headers should be sent with end_stream=true.
  ASSERT_NE(captured_cb, nullptr);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);
  captured_cb();
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, DeferredLocalReplyAfterResetStream) {
  Event::PostCb captured_cb;
  EXPECT_CALL(mock_connection_.dispatcher_, post(_))
      .WillOnce(Invoke([&captured_cb](Event::PostCb cb) { captured_cb = std::move(cb); }));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());

  // Reset the stream before the deferred callback runs, which sets upstream_request_ to nullptr.
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  // The callback should detect upstream_request_ is null and return early.
  ASSERT_NE(captured_cb, nullptr);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);
  captured_cb();
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, OnEventSuppressedDuringPendingLocalReply) {
  Event::PostCb captured_cb;
  EXPECT_CALL(mock_connection_.dispatcher_, post(_))
      .WillOnce(Invoke([&captured_cb](Event::PostCb cb) { captured_cb = std::move(cb); }));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, true).ok());

  // While the deferred callback is pending, onEvent should not call onResetStream.
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// =============================================================================
// HttpTcpBridge tests for ABI callback edge cases.
// =============================================================================

class HttpTcpBridgeAbiEdgeCasesTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeAbiEdgeCasesTest() { createBridge("upstream_bridge_abi_edge_cases"); }
};

TEST_F(HttpTcpBridgeAbiEdgeCasesTest, EdgeCaseCallbacksExercised) {
  // The abi_edge_cases module exercises during encode_headers:
  // - get_request_header with out-of-range index (returns false).
  // - get_request_header without total_count_out (null pointer).
  // - get_request_headers_size.
  // - set_response_header and add_response_header.
  // - append_response_body with non-empty and empty data.
  // - set_response_body with empty data.
  // - append_request_buffer with empty data.
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());
}

// =============================================================================
// Edge case tests for connection and event handling.
// =============================================================================

TEST_F(HttpTcpBridgeTest, ReadDisableConnectionNotOpen) {
  EXPECT_CALL(mock_connection_, state()).WillOnce(Return(Network::Connection::State::Closed));
  EXPECT_CALL(mock_connection_, readDisable(_)).Times(0);
  bridge_->readDisable(true);
}

TEST_F(HttpTcpBridgeTest, OnEventConnectedIsNoOp) {
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(HttpTcpBridgeTest, OnEventLocalClose) {
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _));
  bridge_->onEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpTcpBridgeTest, OnEventAfterResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  // After resetStream, upstream_request_ is null. onEvent should not call onResetStream.
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpTcpBridgeTest, OnEventSuppressedWhenResponseHeadersSent) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // Trigger response headers being sent.
  Buffer::OwnedImpl data("data");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  bridge_->onUpstreamData(data, false);

  // Now onEvent should be suppressed because response_headers_sent_ is true.
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpTcpBridgeTest, WatermarksAfterResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  // After resetStream, watermark callbacks should be no-ops.
  EXPECT_CALL(mock_upstream_to_downstream_, onAboveWriteBufferHighWatermark()).Times(0);
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_upstream_to_downstream_, onBelowWriteBufferLowWatermark()).Times(0);
  bridge_->onBelowWriteBufferLowWatermark();
}

TEST_F(HttpTcpBridgeTest, OnUpstreamDataAfterResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  // After resetStream, upstream_request_ is null. onUpstreamData should be a no-op.
  Buffer::OwnedImpl data("response");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeTest, EncodeDataStreamingModeReplacesBuffer) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // In streaming mode (WaitingData state), each encodeData replaces the request_buffer_.
  Buffer::OwnedImpl data1("first");
  bridge_->encodeData(data1, false);

  Buffer::OwnedImpl data2("second");
  bridge_->encodeData(data2, true);
}

TEST_F(HttpTcpBridgeTest, OnUpstreamDataMultipleCallsSendsHeadersOnce) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // First call sends headers.
  Buffer::OwnedImpl data1("data1");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  bridge_->onUpstreamData(data1, false);

  // Second call should not send headers again.
  Buffer::OwnedImpl data2("data2");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  bridge_->onUpstreamData(data2, false);
}

TEST_F(HttpTcpBridgeTest, SendDataToUpstreamEmptyBufferNotEndStream) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  // Empty buffer with end_stream=false should not write to the connection.
  Buffer::OwnedImpl empty_data;
  bridge_->encodeData(empty_data, false);
}

TEST_F(HttpTcpBridgeTest, BytesMeter) {
  auto& meter = bridge_->bytesMeter();
  EXPECT_NE(meter, nullptr);
}

TEST_F(HttpTcpBridgeTest, SetAccount) {
  // setAccount is a no-op but should not crash.
  bridge_->setAccount(nullptr);
}

TEST_F(HttpTcpBridgeTest, EnableTcpTunneling) {
  // enableTcpTunneling is a no-op but should not crash.
  bridge_->enableTcpTunneling();
}

TEST_F(HttpTcpBridgeTest, EncodeMetadata) {
  // encodeMetadata is a no-op but should not crash.
  Envoy::Http::MetadataMapVector metadata;
  bridge_->encodeMetadata(metadata);
}

// =============================================================================
// DynamicModuleGenericConnPoolFactory tests for config.cc coverage.
// =============================================================================

class DynamicModuleGenericConnPoolFactoryTest : public ::testing::Test {
public:
  DynamicModuleGenericConnPoolFactoryTest()
      : host_(std::make_shared<NiceMock<Upstream::MockHost>>()) {
    setTestModulesSearchPath();

    cm_.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm_.thread_local_cluster_, tcpConnPool(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(Upstream::TcpPoolData([]() {}, &mock_pool_)));
  }

  envoy::extensions::upstreams::http::dynamic_modules::v3::Config createProtoConfig() {
    envoy::extensions::upstreams::http::dynamic_modules::v3::Config config;
    config.mutable_dynamic_module_config()->set_name("upstream_bridge_no_op");
    config.set_bridge_name("test_bridge");
    return config;
  }

  DynamicModuleGenericConnPoolFactory factory_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Envoy::Tcp::ConnectionPool::MockInstance mock_pool_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
};

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateSuccess) {
  auto config = createProtoConfig();
  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CacheHit) {
  auto config = createProtoConfig();
  // First call creates the config.
  auto pool1 = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool1, nullptr);

  // Second call with the same config should hit the cache.
  auto pool2 = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool2, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, ModuleLoadFailure) {
  auto config = createProtoConfig();
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_EQ(pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, BridgeConfigCreateFailure) {
  auto config = createProtoConfig();
  config.mutable_dynamic_module_config()->set_name("upstream_bridge_config_new_fail");

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_EQ(pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, NoBridgeConfig) {
  auto config = createProtoConfig();
  config.clear_bridge_config();

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, BridgeConfigParseFailure) {
  // Construct a config with bridge_config whose Any has a type_url matching StringValue but
  // with invalid protobuf wire format bytes so that anyToBytes fails during unpack.
  auto config = createProtoConfig();
  config.set_bridge_name("parse_fail_test");

  auto* any = config.mutable_bridge_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  // Wire type 7 (invalid) with field number 1: tag byte = (1 << 3) | 7 = 0x0F.
  // Followed by truncated data to trigger ParseFromString failure.
  any->set_value(std::string("\x0F\xFF\xFF", 3));

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_EQ(pool, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, NameAndCategory) {
  EXPECT_EQ("envoy.upstreams.http.dynamic_modules", factory_.name());
  EXPECT_EQ("envoy.upstreams", factory_.category());
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, InvalidTcpPool) {
  // When tcpConnPool returns nullopt, TcpConnPool::valid() returns false and
  // createGenericConnPool returns nullptr.
  NiceMock<Upstream::MockClusterManager> cm2;
  cm2.initializeThreadLocalClusters({"fake_cluster"});
  EXPECT_CALL(cm2.thread_local_cluster_, tcpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));

  // Use a different bridge name to avoid cache hit.
  auto config = createProtoConfig();
  config.set_bridge_name("invalid_pool_test");

  auto pool = factory_.createGenericConnPool(
      host_, cm2.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_EQ(pool, nullptr);
}

} // namespace
} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
