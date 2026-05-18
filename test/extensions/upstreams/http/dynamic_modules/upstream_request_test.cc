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

TEST_F(HttpTcpBridgeTest, EncodeHeadersNoOp) {
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

TEST_F(HttpTcpBridgeTest, OnUpstreamDataNoOp) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl data("response data");
  bridge_->onUpstreamData(data, false);
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
// HttpTcpBridge tests for stop-and-buffer module (no callbacks called).
// =============================================================================

class HttpTcpBridgeStopAndBufferTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeStopAndBufferTest() { createBridge("upstream_bridge_stop_and_buffer"); }
};

TEST_F(HttpTcpBridgeStopAndBufferTest, EncodeHeadersNoCallbacksCalled) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeStopAndBufferTest, EncodeDataNoCallbacksCalled) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl chunk1("chunk1");
  bridge_->encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2("chunk2");
  bridge_->encodeData(chunk2, true);
}

TEST_F(HttpTcpBridgeStopAndBufferTest, OnUpstreamDataNoCallbacksCalled) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  Buffer::OwnedImpl data("upstream data");
  bridge_->onUpstreamData(data, false);
}

// =============================================================================
// HttpTcpBridge tests for EndStream behavior (module calls send callbacks).
// =============================================================================

class HttpTcpBridgeEndStreamTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeEndStreamTest() { createBridge("upstream_bridge_end_stream"); }
};

TEST_F(HttpTcpBridgeEndStreamTest, EncodeDataSendsResponse) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Buffer::OwnedImpl data("request data");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeEndStreamTest, EncodeTrailersSendsResponse) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"key", "value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeEndStreamTest, OnUpstreamDataSendsResponseHeadersAndData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, false).ok());

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Buffer::OwnedImpl data("upstream response");
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeEndStreamTest, EncodeHeadersExercisesAbiCallbacks) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {"x-custom", "value"}};

  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_TRUE(status.ok());
}

// =============================================================================
// HttpTcpBridge tests for encodeHeaders send_response (module sends response
// directly during encode_headers).
// =============================================================================

class HttpTcpBridgeHeadersEndStreamTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeHeadersEndStreamTest() { createBridge("upstream_bridge_headers_end_stream"); }
};

TEST_F(HttpTcpBridgeHeadersEndStreamTest, SendResponseWithBody) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, SendResponseWithoutBody) {
  createBridge("upstream_bridge_headers_end_stream_no_body");

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_TRUE(status.ok());
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, OnEventAfterResponseStarted) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_TRUE(bridge_->encodeHeaders(headers, true).ok());

  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _));
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
  // - get_request_header with out-of-range index.
  // - get_request_header without total_count_out.
  // - get_request_headers_size.
  // - send_response_headers and send_response_data.
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

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

  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpTcpBridgeTest, WatermarksAfterResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  EXPECT_CALL(mock_upstream_to_downstream_, onAboveWriteBufferHighWatermark()).Times(0);
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_upstream_to_downstream_, onBelowWriteBufferLowWatermark()).Times(0);
  bridge_->onBelowWriteBufferLowWatermark();
}

TEST_F(HttpTcpBridgeTest, OnUpstreamDataAfterResetStream) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  Buffer::OwnedImpl data("response");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeTest, SendCallbacksAfterResetStreamAreNoOps) {
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);
  EXPECT_CALL(mock_upstream_to_downstream_, decodeTrailers(_)).Times(0);

  bridge_->sendResponse(200, nullptr, 0, "body");
  bridge_->sendResponseHeaders(200, nullptr, 0, false);
  bridge_->sendResponseData("data", false);
  bridge_->sendResponseTrailers(nullptr, 0);
}

TEST_F(HttpTcpBridgeTest, SendResponseTrailersWithContent) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  bridge_->sendResponseHeaders(200, nullptr, 0, false);

  envoy_dynamic_module_type_module_http_header trailers[2];
  trailers[0] = {"grpc-status", 11, "0", 1};
  trailers[1] = {"grpc-message", 12, "ok", 2};

  EXPECT_CALL(mock_upstream_to_downstream_, decodeTrailers(_));
  bridge_->sendResponseTrailers(trailers, 2);
}

TEST_F(HttpTcpBridgeTest, BytesMeter) {
  auto& meter = bridge_->bytesMeter();
  EXPECT_NE(meter, nullptr);
}

TEST_F(HttpTcpBridgeTest, SetAccount) { bridge_->setAccount(nullptr); }

TEST_F(HttpTcpBridgeTest, EnableTcpTunneling) { bridge_->enableTcpTunneling(); }

TEST_F(HttpTcpBridgeTest, EncodeMetadata) {
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
  auto pool1 = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool1, nullptr);

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
  auto config = createProtoConfig();
  config.set_bridge_name("parse_fail_test");

  auto* any = config.mutable_bridge_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value(std::string("\x0F\xFF\xFF", 3));

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_EQ(pool, nullptr);
}

// Smoke test that a ``google.protobuf.Struct`` ``bridge_config`` is accepted by the factory.
// See https://github.com/envoyproxy/envoy/issues/44733. The previous ``MessageUtil::anyToBytes``
// implementation did not handle ``Struct`` per the API contract; ``knownAnyToBytes`` unpacks the
// ``Struct`` and serializes it to a JSON string before handing it to the bridge module. Note
// that the ``upstream_bridge_no_op`` test module deliberately ignores the config bytes, so this
// test only locks in that the ``Struct`` unpack and JSON serialization path does not surface as
// an error to the factory; the byte-level semantics are covered by the HTTP per-route
// integration test which exercises the same ``knownAnyToBytes`` helper.
TEST_F(DynamicModuleGenericConnPoolFactoryTest, BridgeConfigWithStruct) {
  auto config = createProtoConfig();
  config.set_bridge_name("struct_test_bridge");

  Protobuf::Struct struct_value;
  (*struct_value.mutable_fields())["key"].set_string_value("value");
  config.mutable_bridge_config()->PackFrom(struct_value);

  auto pool = factory_.createGenericConnPool(
      host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, absl::nullopt, nullptr, config);
  EXPECT_NE(pool, nullptr);
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
  NiceMock<Upstream::MockClusterManager> cm2;
  cm2.initializeThreadLocalClusters({"fake_cluster"});
  EXPECT_CALL(cm2.thread_local_cluster_, tcpConnPool(_, _, _)).WillOnce(Return(absl::nullopt));

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
