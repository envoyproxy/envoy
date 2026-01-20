#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/upstreams/http/dynamic_modules/http_tcp_bridge.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/tcp/mocks.h"

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

class DynamicModuleHttpTcpBridgeTest : public ::testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_no_op", "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config_result = DynamicModuleHttpTcpBridgeConfig::create(
        "test_bridge", "test_config", std::move(dynamic_module.value()));
    ASSERT_TRUE(config_result.ok()) << config_result.status().message();
    config_ = std::move(config_result.value());

    conn_data_ = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
    EXPECT_CALL(*conn_data_, connection())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, enableHalfClose(true)).Times(AnyNumber());

    // Set up mock route.
    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(upstream_to_downstream_, route())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(*route_));
  }

  void createBridge() {
    bridge_ = std::make_unique<DynamicModuleHttpTcpBridge>(config_, &upstream_to_downstream_,
                                                           std::move(conn_data_));
  }

  DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  std::unique_ptr<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>> conn_data_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<Router::MockUpstreamToDownstream> upstream_to_downstream_;
  std::shared_ptr<NiceMock<Router::MockRoute>> route_;
  std::unique_ptr<DynamicModuleHttpTcpBridge> bridge_;

  Envoy::Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":path", "/test"}, {":authority", "host.example.com"}};
};

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeHeadersBasic) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeHeadersEndStream) {
  createBridge();

  EXPECT_CALL(connection_, write(_, true));
  auto status = bridge_->encodeHeaders(request_headers_, true);
  EXPECT_TRUE(status.ok());
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeDataBasic) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection_, write(_, false));
  bridge_->encodeData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeDataEndStream) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection_, write(_, true));
  bridge_->encodeData(data, true);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeTrailers) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer-key", "trailer-value"}};
  EXPECT_CALL(connection_, write(_, true));
  bridge_->encodeTrailers(trailers);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, OnUpstreamDataBasic) {
  createBridge();

  // First encode headers to set up the request.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Simulate upstream data.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, false));
  bridge_->onUpstreamData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, OnUpstreamDataEndStream) {
  createBridge();

  // First encode headers.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Simulate upstream data with end_stream.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true));
  bridge_->onUpstreamData(data, true);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, ReadDisable) {
  createBridge();

  EXPECT_CALL(connection_, readDisable(true));
  bridge_->readDisable(true);

  EXPECT_CALL(connection_, readDisable(false));
  bridge_->readDisable(false);

  // Once the connection is closed, readDisable should not be called.
  connection_.state_ = Network::Connection::State::Closed;
  EXPECT_CALL(connection_, readDisable(_)).Times(0);
  bridge_->readDisable(true);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, ResetStream) {
  createBridge();

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  bridge_->resetStream();
}

TEST_F(DynamicModuleHttpTcpBridgeTest, OnEventRemoteClose) {
  createBridge();

  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, _));
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, OnEventLocalClose) {
  createBridge();

  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, _));
  bridge_->onEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, OnEventConnected) {
  createBridge();

  // Connected event should not trigger reset.
  EXPECT_CALL(upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, Watermarks) {
  createBridge();

  EXPECT_CALL(upstream_to_downstream_, onAboveWriteBufferHighWatermark());
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(upstream_to_downstream_, onBelowWriteBufferLowWatermark());
  bridge_->onBelowWriteBufferLowWatermark();
}

TEST_F(DynamicModuleHttpTcpBridgeTest, BytesMeter) {
  createBridge();

  EXPECT_NE(bridge_->bytesMeter(), nullptr);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EncodeMetadata) {
  createBridge();

  // encodeMetadata is a no-op, just ensure it doesn't crash.
  Envoy::Http::MetadataMapVector metadata_map_vector;
  bridge_->encodeMetadata(metadata_map_vector);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, EnableTcpTunneling) {
  createBridge();

  // enableTcpTunneling should enable half-close again.
  EXPECT_CALL(connection_, enableHalfClose(true));
  bridge_->enableTcpTunneling();
}

TEST_F(DynamicModuleHttpTcpBridgeTest, SetAccount) {
  createBridge();

  // setAccount is a no-op, just ensure it doesn't crash.
  bridge_->setAccount(nullptr);
}

// ---------------------- ABI callback tests ------------------------

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRequestHeadersCount) {
  createBridge();

  // Before encodeHeaders, request_headers_ is nullptr.
  EXPECT_EQ(bridge_->getRequestHeadersCount(), 0);

  // After encodeHeaders, request_headers_ should be set.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  EXPECT_EQ(bridge_->getRequestHeadersCount(), 3); // :method, :path, :authority
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRequestHeader) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Get header at index 0.
  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_TRUE(bridge_->getRequestHeader(0, &key, &value));
  EXPECT_NE(key.ptr, nullptr);
  EXPECT_GT(key.length, 0);

  // Get header at index 1.
  EXPECT_TRUE(bridge_->getRequestHeader(1, &key, &value));

  // Get header at index 2.
  EXPECT_TRUE(bridge_->getRequestHeader(2, &key, &value));

  // Index out of bounds.
  EXPECT_FALSE(bridge_->getRequestHeader(100, &key, &value));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRequestHeaderBeforeEncode) {
  createBridge();

  // Before encodeHeaders, request_headers_ is nullptr.
  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_FALSE(bridge_->getRequestHeader(0, &key, &value));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRequestHeaderValue) {
  createBridge();

  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Get existing header value.
  envoy_dynamic_module_type_module_buffer key = {":method", 7};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_TRUE(bridge_->getRequestHeaderValue(key, &value));
  EXPECT_EQ(absl::string_view(value.ptr, value.length), "POST");

  // Get non-existing header value.
  envoy_dynamic_module_type_module_buffer non_existing_key = {"non-existing", 12};
  EXPECT_FALSE(bridge_->getRequestHeaderValue(non_existing_key, &value));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRequestHeaderValueBeforeEncode) {
  createBridge();

  // Before encodeHeaders, request_headers_ is nullptr.
  envoy_dynamic_module_type_module_buffer key = {":method", 7};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_FALSE(bridge_->getRequestHeaderValue(key, &value));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, UpstreamBufferOperations) {
  createBridge();

  // Get initial empty buffer.
  uintptr_t buffer_ptr = 0;
  size_t length = 0;
  bridge_->getUpstreamBuffer(&buffer_ptr, &length);
  EXPECT_NE(buffer_ptr, 0);
  EXPECT_EQ(length, 0);

  // Set buffer.
  const char* data = "test data";
  envoy_dynamic_module_type_module_buffer buffer_data = {data, 9};
  bridge_->setUpstreamBuffer(buffer_data);
  bridge_->getUpstreamBuffer(&buffer_ptr, &length);
  EXPECT_EQ(length, 9);

  // Append buffer.
  const char* more_data = " more";
  envoy_dynamic_module_type_module_buffer more_buffer_data = {more_data, 5};
  bridge_->appendUpstreamBuffer(more_buffer_data);
  bridge_->getUpstreamBuffer(&buffer_ptr, &length);
  EXPECT_EQ(length, 14);

  // Set empty buffer.
  envoy_dynamic_module_type_module_buffer empty_data = {nullptr, 0};
  bridge_->setUpstreamBuffer(empty_data);
  bridge_->getUpstreamBuffer(&buffer_ptr, &length);
  EXPECT_EQ(length, 0);

  // Append empty buffer (should be no-op).
  bridge_->appendUpstreamBuffer(empty_data);
  bridge_->getUpstreamBuffer(&buffer_ptr, &length);
  EXPECT_EQ(length, 0);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, DownstreamBufferOperations) {
  createBridge();

  // Before onUpstreamData, downstream_buffer_ is nullptr.
  uintptr_t buffer_ptr = 0;
  size_t length = 0;
  bridge_->getDownstreamBuffer(&buffer_ptr, &length);
  EXPECT_EQ(buffer_ptr, 0);
  EXPECT_EQ(length, 0);

  // Drain with no buffer should be safe.
  bridge_->drainDownstreamBuffer(10);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, ResponseHeaderOperations) {
  createBridge();

  // Set response header before sending.
  envoy_dynamic_module_type_module_buffer key = {"x-custom-header", 15};
  envoy_dynamic_module_type_module_buffer value = {"custom-value", 12};
  EXPECT_TRUE(bridge_->setResponseHeader(key, value));

  // Add response header before sending.
  envoy_dynamic_module_type_module_buffer key2 = {"x-another-header", 16};
  envoy_dynamic_module_type_module_buffer value2 = {"another-value", 13};
  EXPECT_TRUE(bridge_->addResponseHeader(key2, value2));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, ResponseHeaderAfterSent) {
  createBridge();

  // First encode headers to set up the request.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Simulate upstream data which will send response headers.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, false));
  bridge_->onUpstreamData(data, false);

  // After headers are sent, setting/adding should fail.
  envoy_dynamic_module_type_module_buffer key = {"x-custom-header", 15};
  envoy_dynamic_module_type_module_buffer value = {"custom-value", 12};
  EXPECT_FALSE(bridge_->setResponseHeader(key, value));
  EXPECT_FALSE(bridge_->addResponseHeader(key, value));
}

TEST_F(DynamicModuleHttpTcpBridgeTest, SendResponseWithBuffer) {
  createBridge();

  // First encode headers to set up the request.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Simulate upstream data to set downstream_buffer_.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, false));
  bridge_->onUpstreamData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, SendResponseWithoutBuffer) {
  createBridge();

  // First encode headers to set up the request.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Call sendResponse without downstream_buffer_ set.
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true));
  bridge_->sendResponse(true);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRouteNameWithVirtualHost) {
  // Set up virtual host mock before creating bridge.
  auto virtual_host = std::make_shared<NiceMock<Router::MockVirtualHost>>();
  Router::VirtualHostConstSharedPtr vhost_ptr = virtual_host;
  NiceMock<Router::MockConfig> route_config;
  std::string route_name = "test-route";

  EXPECT_CALL(*route_, virtualHost()).WillRepeatedly(ReturnRef(vhost_ptr));
  EXPECT_CALL(*virtual_host, routeConfig()).WillRepeatedly(ReturnRef(route_config));
  EXPECT_CALL(route_config, name()).WillRepeatedly(ReturnRef(route_name));

  createBridge();

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bridge_->getRouteName(&result);
  EXPECT_EQ(absl::string_view(result.ptr, result.length), "test-route");
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetRouteNameWithoutVirtualHost) {
  // Return nullptr for virtual host.
  Router::VirtualHostConstSharedPtr null_vhost{nullptr};
  EXPECT_CALL(*route_, virtualHost()).WillRepeatedly(ReturnRef(null_vhost));

  createBridge();

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bridge_->getRouteName(&result);
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetClusterNameWithRouteEntry) {
  createBridge();

  // Set up route entry mock.
  auto route_entry = std::make_shared<NiceMock<Router::MockRouteEntry>>();
  std::string cluster_name = "test-cluster";

  EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(route_entry.get()));
  EXPECT_CALL(*route_entry, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  // Re-create bridge to pick up route entry.
  conn_data_ = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*conn_data_, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, enableHalfClose(true)).Times(AnyNumber());
  bridge_ = std::make_unique<DynamicModuleHttpTcpBridge>(config_, &upstream_to_downstream_,
                                                         std::move(conn_data_));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bridge_->getClusterName(&result);
  EXPECT_EQ(absl::string_view(result.ptr, result.length), "test-cluster");
}

TEST_F(DynamicModuleHttpTcpBridgeTest, GetClusterNameWithoutRouteEntry) {
  createBridge();

  // Route entry is nullptr by default in the mock.
  EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(nullptr));

  // Re-create bridge to pick up route entry.
  conn_data_ = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*conn_data_, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection_));
  EXPECT_CALL(connection_, enableHalfClose(true)).Times(AnyNumber());
  bridge_ = std::make_unique<DynamicModuleHttpTcpBridge>(config_, &upstream_to_downstream_,
                                                         std::move(conn_data_));

  envoy_dynamic_module_type_envoy_buffer result = {nullptr, 0};
  bridge_->getClusterName(&result);
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

// ---------------------- ABI impl (extern "C") tests ------------------------
// These tests verify the extern "C" functions in abi_impl.cc work correctly,
// including null pointer handling.

TEST_F(DynamicModuleHttpTcpBridgeTest, AbiImplNullPointerHandling) {
  // Test all extern "C" functions with nullptr.
  EXPECT_EQ(
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_count(nullptr), 0);

  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
      nullptr, 0, &key, &value));

  envoy_dynamic_module_type_module_buffer mod_key = {"key", 3};
  EXPECT_FALSE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header_value(
      nullptr, mod_key, &value));

  uintptr_t buffer_ptr = 123;
  size_t length = 123;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(nullptr, &buffer_ptr,
                                                                             &length);
  EXPECT_EQ(buffer_ptr, 0);
  EXPECT_EQ(length, 0);

  envoy_dynamic_module_type_module_buffer data = {"data", 4};
  // These should not crash with nullptr.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_upstream_buffer(nullptr, data);
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_upstream_buffer(nullptr, data);

  buffer_ptr = 123;
  length = 123;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_downstream_buffer(nullptr, &buffer_ptr,
                                                                               &length);
  EXPECT_EQ(buffer_ptr, 0);
  EXPECT_EQ(length, 0);

  // Should not crash with nullptr.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_downstream_buffer(nullptr, 10);

  envoy_dynamic_module_type_module_buffer header_key = {"key", 3};
  envoy_dynamic_module_type_module_buffer header_value = {"value", 5};
  EXPECT_FALSE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
      nullptr, header_key, header_value));
  EXPECT_FALSE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
      nullptr, header_key, header_value));

  // Should not crash with nullptr.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(nullptr, false);

  envoy_dynamic_module_type_envoy_buffer result = {reinterpret_cast<const char*>(123), 123};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_route_name(nullptr, &result);
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);

  result = {reinterpret_cast<const char*>(123), 123};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_cluster_name(nullptr, &result);
  EXPECT_EQ(result.ptr, nullptr);
  EXPECT_EQ(result.length, 0);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, AbiImplWithValidBridge) {
  createBridge();

  // Encode headers first.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Test with valid bridge pointer.
  auto bridge_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());

  EXPECT_EQ(
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_count(bridge_ptr),
      3);

  envoy_dynamic_module_type_envoy_buffer key = {nullptr, 0};
  envoy_dynamic_module_type_envoy_buffer value = {nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
      bridge_ptr, 0, &key, &value));

  envoy_dynamic_module_type_module_buffer mod_key = {":method", 7};
  EXPECT_TRUE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header_value(
      bridge_ptr, mod_key, &value));

  uintptr_t buffer_ptr = 0;
  size_t length = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(bridge_ptr,
                                                                             &buffer_ptr, &length);
  EXPECT_NE(buffer_ptr, 0);

  envoy_dynamic_module_type_module_buffer data = {"data", 4};
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_upstream_buffer(bridge_ptr, data);
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(bridge_ptr,
                                                                             &buffer_ptr, &length);
  EXPECT_EQ(length, 4);

  envoy_dynamic_module_callback_upstream_http_tcp_bridge_append_upstream_buffer(bridge_ptr, data);
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_upstream_buffer(bridge_ptr,
                                                                             &buffer_ptr, &length);
  EXPECT_EQ(length, 8);

  envoy_dynamic_module_type_module_buffer header_key = {"x-test", 6};
  envoy_dynamic_module_type_module_buffer header_value = {"test-value", 10};
  EXPECT_TRUE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_set_response_header(
      bridge_ptr, header_key, header_value));
  EXPECT_TRUE(envoy_dynamic_module_callback_upstream_http_tcp_bridge_add_response_header(
      bridge_ptr, header_key, header_value));
}

// ---------------------- EndStream status tests ------------------------
// These tests use a module that returns EndStream status.

class DynamicModuleHttpTcpBridgeEndStreamTest : public ::testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_end_stream", "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config_result = DynamicModuleHttpTcpBridgeConfig::create(
        "test_bridge", "test_config", std::move(dynamic_module.value()));
    ASSERT_TRUE(config_result.ok()) << config_result.status().message();
    config_ = std::move(config_result.value());

    conn_data_ = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
    EXPECT_CALL(*conn_data_, connection())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, enableHalfClose(true)).Times(AnyNumber());

    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(upstream_to_downstream_, route())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(*route_));
  }

  void createBridge() {
    bridge_ = std::make_unique<DynamicModuleHttpTcpBridge>(config_, &upstream_to_downstream_,
                                                           std::move(conn_data_));
  }

  DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  std::unique_ptr<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>> conn_data_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<Router::MockUpstreamToDownstream> upstream_to_downstream_;
  std::shared_ptr<NiceMock<Router::MockRoute>> route_;
  std::unique_ptr<DynamicModuleHttpTcpBridge> bridge_;

  Envoy::Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":path", "/test"}, {":authority", "host.example.com"}};
};

TEST_F(DynamicModuleHttpTcpBridgeEndStreamTest, EncodeHeadersEndStreamStatus) {
  createBridge();

  // Module returns EndStream, which should trigger sendDataToDownstream with end_stream=true.
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true));
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());
}

TEST_F(DynamicModuleHttpTcpBridgeEndStreamTest, EncodeDataEndStreamStatus) {
  createBridge();

  // encodeHeaders also returns EndStream, so we get headers sent there.
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Now call encodeData - module returns EndStream.
  Buffer::OwnedImpl data("test data");
  bridge_->encodeData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeEndStreamTest, OnUpstreamDataEndStreamStatus) {
  createBridge();

  // First set up by calling encodeHeaders.
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false)).Times(AnyNumber());
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Now call onUpstreamData - module returns EndStream.
  Buffer::OwnedImpl data("response data");
  bridge_->onUpstreamData(data, false);
}

// ---------------------- StopAndBuffer status tests ------------------------
// These tests use a module that returns StopAndBuffer status.

class DynamicModuleHttpTcpBridgeStopAndBufferTest : public ::testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_stop_and_buffer",
                                                                "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config_result = DynamicModuleHttpTcpBridgeConfig::create(
        "test_bridge", "test_config", std::move(dynamic_module.value()));
    ASSERT_TRUE(config_result.ok()) << config_result.status().message();
    config_ = std::move(config_result.value());

    conn_data_ = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
    EXPECT_CALL(*conn_data_, connection())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, enableHalfClose(true)).Times(AnyNumber());

    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(upstream_to_downstream_, route())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(*route_));
  }

  void createBridge() {
    bridge_ = std::make_unique<DynamicModuleHttpTcpBridge>(config_, &upstream_to_downstream_,
                                                           std::move(conn_data_));
  }

  DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  std::unique_ptr<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>> conn_data_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<Router::MockUpstreamToDownstream> upstream_to_downstream_;
  std::shared_ptr<NiceMock<Router::MockRoute>> route_;
  std::unique_ptr<DynamicModuleHttpTcpBridge> bridge_;

  Envoy::Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":path", "/test"}, {":authority", "host.example.com"}};
};

TEST_F(DynamicModuleHttpTcpBridgeStopAndBufferTest, EncodeDataStopAndBufferStatus) {
  createBridge();

  // First encode headers.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Now call encodeData - module returns StopAndBuffer.
  // Connection write should NOT be called because we're buffering.
  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection_, write(_, _)).Times(0);
  bridge_->encodeData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeStopAndBufferTest, EncodeDataStopAndBufferAtEndStream) {
  createBridge();

  // First encode headers.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Call encodeData with end_stream=true and StopAndBuffer - should log error.
  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection_, write(_, _)).Times(0);
  bridge_->encodeData(data, true);
}

TEST_F(DynamicModuleHttpTcpBridgeStopAndBufferTest, OnUpstreamDataStopAndBufferStatus) {
  createBridge();

  // First encode headers.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Now call onUpstreamData - module returns StopAndBuffer.
  // Downstream should NOT receive data because we're buffering.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, _)).Times(0);
  bridge_->onUpstreamData(data, false);
}

TEST_F(DynamicModuleHttpTcpBridgeStopAndBufferTest, OnUpstreamDataStopAndBufferAtEndStream) {
  createBridge();

  // First encode headers.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Call onUpstreamData with end_stream=true and StopAndBuffer - should log error.
  Buffer::OwnedImpl data("response data");
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, _)).Times(0);
  bridge_->onUpstreamData(data, true);
}

// ---------------------- ABI impl downstream buffer and send_response tests
// ------------------------ These tests cover the ABI impl functions with valid bridge and
// downstream buffer.

TEST_F(DynamicModuleHttpTcpBridgeTest, AbiImplSendResponseWithValidBridge) {
  createBridge();

  // Encode headers first.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  auto bridge_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());

  // Test sendResponse via ABI - this covers the non-null path in abi_impl.cc.
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, true));
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(bridge_ptr, true);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, AbiImplDrainDownstreamBufferWithValidBridge) {
  createBridge();

  auto bridge_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());

  // Test drainDownstreamBuffer via ABI with valid bridge.
  // Since downstream_buffer_ is nullptr, this tests the null-safe path in the method.
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_drain_downstream_buffer(bridge_ptr, 10);
}

TEST_F(DynamicModuleHttpTcpBridgeTest, AbiImplGetDownstreamBufferWithValidBridge) {
  createBridge();

  auto bridge_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());

  // Test getDownstreamBuffer via ABI with valid bridge.
  uintptr_t buffer_ptr = 123;
  size_t length = 123;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_downstream_buffer(
      bridge_ptr, &buffer_ptr, &length);
  // downstream_buffer_ is nullptr, so should return 0.
  EXPECT_EQ(buffer_ptr, 0);
  EXPECT_EQ(length, 0);
}

// Test sendResponse when downstream_buffer_ has data.
TEST_F(DynamicModuleHttpTcpBridgeTest, SendResponseWithDataViaBridge) {
  createBridge();

  // Encode headers first.
  EXPECT_CALL(connection_, write(_, false)).Times(AnyNumber());
  auto status = bridge_->encodeHeaders(request_headers_, false);
  EXPECT_TRUE(status.ok());

  // Set response headers.
  envoy_dynamic_module_type_module_buffer key = {"x-test", 6};
  envoy_dynamic_module_type_module_buffer value = {"value", 5};
  EXPECT_TRUE(bridge_->setResponseHeader(key, value));

  // Call sendResponse directly on bridge (not via ABI).
  EXPECT_CALL(upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(upstream_to_downstream_, decodeData(_, false));
  bridge_->sendResponse(false);
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
