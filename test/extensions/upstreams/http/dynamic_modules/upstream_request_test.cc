#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/upstreams/http/dynamic_modules/config.h"
#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::HasStatusMessage;
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
  EXPECT_OK(module);
  auto config_or = BridgeConfig::create("test_bridge", "", std::move(module.value()));
  EXPECT_OK(config_or);
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
  ASSERT_OK(module);

  auto config = BridgeConfig::create("test_bridge", "test_config", std::move(module.value()));
  ASSERT_OK(config);
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST_F(BridgeConfigTest, CreateFailConfigNewReturnsNull) {
  auto module = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      "upstream_bridge_config_new_fail", false, false);
  ASSERT_OK(module);

  auto config = BridgeConfig::create("test_bridge", "test_config", std::move(module.value()));
  ASSERT_THAT(config, HasStatusMessage(testing::HasSubstr(
                          "failed to initialize dynamic module bridge configuration")));
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
    // Destroying an earlier bridge closes the shared mock connection, so reopen it for the next.
    bridge_.reset();
    mock_connection_.state_ = Network::Connection::State::Open;
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
  EXPECT_OK(status);
}

TEST_F(HttpTcpBridgeTest, EncodeHeadersEndOfStream) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};

  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_OK(status);
}

TEST_F(HttpTcpBridgeTest, EncodeData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data("hello");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeTest, EncodeTrailers) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer-key", "trailer-value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeTest, OnUpstreamDataNoOp) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data("response data");
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeTest, RequestBufferRemainsReadableAfterEncodeData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data("hello");
  bridge_->encodeData(data, false);

  // Reading the request buffer after encodeData returns must not touch freed storage.
  auto* envoy_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());
  size_t chunks =
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer_chunks_size(
          envoy_ptr);
  ASSERT_EQ(1, chunks);
  std::vector<envoy_dynamic_module_type_envoy_buffer> result(chunks);
  size_t result_length = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(
      envoy_ptr, result.data(), &result_length);
  ASSERT_EQ(1, result_length);
  EXPECT_EQ("hello", absl::string_view(result[0].ptr, result[0].length));
}

TEST_F(HttpTcpBridgeTest, ResponseBufferRemainsReadableAfterOnUpstreamData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Buffer::OwnedImpl data("world");
  bridge_->onUpstreamData(data, false);

  // Reading the response buffer after onUpstreamData returns must not touch freed storage.
  auto* envoy_ptr =
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(bridge_.get());
  size_t chunks =
      envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer_chunks_size(
          envoy_ptr);
  ASSERT_EQ(1, chunks);
  std::vector<envoy_dynamic_module_type_envoy_buffer> result(chunks);
  size_t result_length = 0;
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
      envoy_ptr, result.data(), &result_length);
  ASSERT_EQ(1, result_length);
  EXPECT_EQ("world", absl::string_view(result[0].ptr, result[0].length));
}

TEST_F(HttpTcpBridgeTest, OnUpstreamConnectionClose) {
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _));
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpTcpBridgeTest, ResetStream) {
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();
  // The reset closed the connection, so destroying the bridge closes it no second time.
  bridge_.reset();
}

TEST_F(HttpTcpBridgeTest, ReadDisable) {
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
  EXPECT_THAT(status, HasStatusMessage("dynamic module bridge is null"));
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
  EXPECT_OK(status);
}

TEST_F(HttpTcpBridgeStopAndBufferTest, EncodeDataNoCallbacksCalled) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  Buffer::OwnedImpl chunk1("chunk1");
  bridge_->encodeData(chunk1, false);

  Buffer::OwnedImpl chunk2("chunk2");
  bridge_->encodeData(chunk2, true);
}

TEST_F(HttpTcpBridgeStopAndBufferTest, OnUpstreamDataNoCallbacksCalled) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

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
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Buffer::OwnedImpl data("request data");
  bridge_->encodeData(data, true);
}

TEST_F(HttpTcpBridgeEndStreamTest, EncodeTrailersSendsResponse) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));

  Envoy::Http::TestRequestTrailerMapImpl trailers{{"key", "value"}};
  bridge_->encodeTrailers(trailers);
}

TEST_F(HttpTcpBridgeEndStreamTest, OnUpstreamDataSendsResponseHeadersAndData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Buffer::OwnedImpl data("upstream response");
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeEndStreamTest, EncodeHeadersExercisesAbiCallbacks) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/test"}, {"x-custom", "value"}};

  auto status = bridge_->encodeHeaders(headers, false);
  EXPECT_OK(status);
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
  EXPECT_OK(status);
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, SendResponseWithoutBody) {
  createBridge("upstream_bridge_headers_end_stream_no_body");

  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  auto status = bridge_->encodeHeaders(headers, true);
  EXPECT_OK(status);
}

TEST_F(HttpTcpBridgeHeadersEndStreamTest, OnEventAfterResponseStarted) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, true));

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
  EXPECT_OK(status);
}

// =============================================================================
// HttpTcpBridge tests for terminal send_response_headers and send_response_trailers deferral.
// =============================================================================

class HttpTcpBridgeTerminalResponseTest : public HttpTcpBridgeTest {
public:
  HttpTcpBridgeTerminalResponseTest() { createBridge("upstream_bridge_terminal_response"); }
};

// The module ends the stream with headers only during encode_headers, so the deferred response is a
// single headers frame with end_stream set and no body.
TEST_F(HttpTcpBridgeTerminalResponseTest, EncodeHeadersSendsTerminalHeaders) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);

  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, false));
}

// The module ends the stream with trailers during encode_data, so the deferred response is a
// trailers frame.
TEST_F(HttpTcpBridgeTerminalResponseTest, EncodeDataSendsTerminalTrailers) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeTrailers(_));

  Buffer::OwnedImpl data("request data");
  bridge_->encodeData(data, false);
}

// A non terminal data send keeps the stream open, so it is applied inline during the hook.
TEST_F(HttpTcpBridgeTerminalResponseTest, OnUpstreamDataSendsNonTerminalData) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, false));

  Buffer::OwnedImpl data("upstream response");
  bridge_->onUpstreamData(data, false);
}

// A send_response called outside a module hook is applied inline, since no hook borrow is live.
TEST_F(HttpTcpBridgeTest, SendResponseOutsideHookWithBodyIsInline) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, false));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, true));
  bridge_->sendResponse(200, nullptr, 0, "body");
}

TEST_F(HttpTcpBridgeTest, SendResponseOutsideHookWithoutBodyIsInline) {
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, true));
  EXPECT_CALL(mock_upstream_to_downstream_, decodeData(_, _)).Times(0);
  bridge_->sendResponse(200, nullptr, 0, "");
}

// =============================================================================
// Edge case tests for connection and event handling.
// =============================================================================

TEST_F(HttpTcpBridgeTest, ReadDisableConnectionNotOpen) {
  ON_CALL(mock_connection_, state()).WillByDefault(Return(Network::Connection::State::Closed));
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
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _)).Times(0);
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Only the module knows where its protocol's exchange ends, so destroying the bridge closes a
// connection that is still open instead of returning it to the pool, and releases the connection
// before the close so the pool reports the close to no one.
TEST_F(HttpTcpBridgeTest, DestroyReleasesThenClosesTheUpstreamConnection) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, true));
  bool released = false;
  mock_conn_data_->release_callback_ = [&released]() { released = true; };
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_destroyed"))
      .WillOnce(Invoke(
          [&released](Network::ConnectionCloseType, absl::string_view) { EXPECT_TRUE(released); }));
  bridge_.reset();
}

void enableReuse(HttpTcpBridge& bridge) {
  envoy_dynamic_module_callback_upstream_http_tcp_bridge_enable_upstream_connection_reuse(
      static_cast<envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr>(&bridge));
}

enum class RequestEnd { Headers, Data, Trailers };

class HttpTcpBridgeReuseTest : public HttpTcpBridgeTest,
                               public testing::WithParamInterface<RequestEnd> {};

INSTANTIATE_TEST_SUITE_P(RequestEnds, HttpTcpBridgeReuseTest,
                         testing::Values(RequestEnd::Headers, RequestEnd::Data,
                                         RequestEnd::Trailers),
                         [](const testing::TestParamInfo<RequestEnd>& info) -> std::string {
                           switch (info.param) {
                           case RequestEnd::Headers:
                             return "Headers";
                           case RequestEnd::Data:
                             return "Data";
                           case RequestEnd::Trailers:
                             return "Trailers";
                           }
                           return "";
                         });

// A module that enabled reuse gets its open connection back in the pool, released and not closed,
// once the whole request reached the bridge, whichever encode call ended it.
TEST_P(HttpTcpBridgeReuseTest, EnabledReuseReturnsAnOpenConnectionToThePool) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, GetParam() == RequestEnd::Headers));
  if (GetParam() == RequestEnd::Data) {
    Buffer::OwnedImpl body("body");
    bridge_->encodeData(body, true);
  }
  if (GetParam() == RequestEnd::Trailers) {
    Envoy::Http::TestRequestTrailerMapImpl trailers{{"trailer", "value"}};
    bridge_->encodeTrailers(trailers);
  }
  enableReuse(*bridge_);
  bool released = false;
  mock_conn_data_->release_callback_ = [&released]() { released = true; };
  EXPECT_CALL(mock_connection_, close(_, _)).Times(0);
  bridge_.reset();
  EXPECT_TRUE(released);
}

// Every field passes the reuse gate by default, so each row names only the one that refuses.
struct ReuseRefusal {
  std::string name;
  Network::Connection::State state = Network::Connection::State::Open;
  bool reuse_enabled = true;
  bool request_complete = true;
  bool module_ended_data = false;
  bool upstream_ended_data = false;
  bool read_enabled = true;
  bool above_high_watermark = false;
};

class HttpTcpBridgeReuseRefusalTest : public HttpTcpBridgeTest,
                                      public testing::WithParamInterface<ReuseRefusal> {};

INSTANTIATE_TEST_SUITE_P(
    Refusals, HttpTcpBridgeReuseRefusalTest,
    testing::Values(ReuseRefusal{.name = "ReuseNotEnabled", .reuse_enabled = false},
                    ReuseRefusal{.name = "RequestIncomplete", .request_complete = false},
                    ReuseRefusal{.name = "ModuleEndedData", .module_ended_data = true},
                    ReuseRefusal{.name = "UpstreamEndedData", .upstream_ended_data = true},
                    ReuseRefusal{.name = "ReadDisabled", .read_enabled = false},
                    ReuseRefusal{.name = "AboveHighWatermark", .above_high_watermark = true},
                    ReuseRefusal{.name = "Closing", .state = Network::Connection::State::Closing}),
    [](const testing::TestParamInfo<ReuseRefusal>& info) { return info.param.name; });

// Envoy closes a connection whose module did not enable reuse, and with reuse enabled it still
// closes one whose request did not reach the bridge in full, one whose data the module or the
// upstream ended, a read-disabled one, one above its write high watermark and a closing one, and it
// never reads readEnabled() on the closing one.
TEST_P(HttpTcpBridgeReuseRefusalTest, RefusedReuseClosesTheConnection) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, GetParam().request_complete));
  if (GetParam().reuse_enabled) {
    enableReuse(*bridge_);
  }
  if (GetParam().module_ended_data) {
    bridge_->sendUpstreamData("", true);
  }
  if (GetParam().upstream_ended_data) {
    Buffer::OwnedImpl data;
    bridge_->onUpstreamData(data, true);
  }
  ON_CALL(mock_connection_, state()).WillByDefault(Return(GetParam().state));
  ON_CALL(mock_connection_, aboveHighWatermark())
      .WillByDefault(Return(GetParam().above_high_watermark));
  ON_CALL(mock_connection_, readEnabled()).WillByDefault(Return(GetParam().read_enabled));
  if (GetParam().state != Network::Connection::State::Open) {
    EXPECT_CALL(mock_connection_, readEnabled()).Times(0);
  }
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_destroyed"));
  bridge_.reset();
}

class HttpTcpBridgeCloseEventTest : public HttpTcpBridgeTest,
                                    public testing::WithParamInterface<Network::ConnectionEvent> {};

INSTANTIATE_TEST_SUITE_P(
    CloseEvents, HttpTcpBridgeCloseEventTest,
    testing::Values(Network::ConnectionEvent::LocalClose, Network::ConnectionEvent::RemoteClose),
    [](const testing::TestParamInfo<Network::ConnectionEvent>& info) -> std::string {
      return info.param == Network::ConnectionEvent::LocalClose ? "LocalClose" : "RemoteClose";
    });

// The router destroys the bridge inside the callback that reports a close, as
// UpstreamRequest::onResetStream does, so the destructor finds the connection closed and makes no
// second close.
TEST_P(HttpTcpBridgeCloseEventTest, ResetThatDestroysTheBridgeMakesNoSecondClose) {
  EXPECT_CALL(mock_connection_, close(_, _)).Times(0);
  EXPECT_CALL(mock_upstream_to_downstream_, onResetStream(_, _))
      .WillOnce(
          Invoke([this](Envoy::Http::StreamResetReason, absl::string_view) { bridge_.reset(); }));
  mock_connection_.raiseEvent(GetParam());
  bridge_->onEvent(GetParam());
  EXPECT_EQ(nullptr, bridge_);
}

// The pool may free a connection once its close returns, so a bridge destroyed after the pool
// reported the close never reads the connection.
TEST_F(HttpTcpBridgeTest, DestroyAfterACloseEventLeavesTheConnectionAlone) {
  bridge_->onEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_CALL(*mock_conn_data_, connection()).Times(0);
  bridge_.reset();
}

// A draining pool closes a client the release returns to it, so the bridge closes no connection
// the release already closed.
TEST_F(HttpTcpBridgeTest, ReleaseThatClosesTheConnectionMakesNoSecondClose) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, true));
  mock_conn_data_->release_callback_ = [this]() {
    mock_connection_.state_ = Network::Connection::State::Closed;
  };
  EXPECT_CALL(mock_connection_, close(_, _)).Times(0);
  bridge_.reset();
}

// The count sums every write under one bytes sent callback, so data the socket took in full leaves
// nothing to drain, the bridge releases and closes at once, and the callback unregisters once the
// bridge is gone.
TEST_F(HttpTcpBridgeTest, SentUpstreamDataClosesWithoutDraining) {
  bridge_->sendUpstreamData("req", false);
  bridge_->sendUpstreamData("uest", false);
  ASSERT_EQ(1U, mock_connection_.bytes_sent_callbacks_.size());
  // The mock keeps a callback whatever it returns, so each return value is checked here.
  EXPECT_TRUE(mock_connection_.bytes_sent_callbacks_.front()(3));
  EXPECT_TRUE(mock_connection_.bytes_sent_callbacks_.front()(4));
  EXPECT_CALL(*mock_conn_data_, addUpstreamCallbacks(_)).Times(0);
  EXPECT_CALL(mock_connection_,
              close(Network::ConnectionCloseType::NoFlush, "dynamic_module_bridge_destroyed"));
  bridge_.reset();
  EXPECT_FALSE(mock_connection_.bytes_sent_callbacks_.front()(1));
}

// The connection takes no data after the module ended its upstream data, so Envoy drops what the
// module sends after that instead of writing it.
TEST_F(HttpTcpBridgeTest, DataSentAfterTheModuleEndedItsDataIsDropped) {
  EXPECT_CALL(mock_connection_, write(_, true));
  bridge_->sendUpstreamData("", true);
  EXPECT_CALL(mock_connection_, write(_, _)).Times(0);
  bridge_->sendUpstreamData("late", false);
  EXPECT_TRUE(mock_connection_.bytes_sent_callbacks_.empty());
}

class HttpTcpBridgeDrainTest : public HttpTcpBridgeTest {
public:
  // Expects the bridge's destructor to hand the connection to a drainer, which it records. With
  // close_at_once, the drain's close reports itself before it returns.
  void expectDrain(bool close_at_once = false) {
    EXPECT_CALL(*mock_conn_data_, addUpstreamCallbacks(_))
        .WillOnce(Invoke([this](Envoy::Tcp::ConnectionPool::UpstreamCallbacks& callbacks) {
          drainer_ = &callbacks;
        }));
    drain_timer_ = new NiceMock<Event::MockTimer>(&mock_connection_.dispatcher_);
    EXPECT_CALL(*drain_timer_, enableTimer(std::chrono::milliseconds(1000), _));
    if (close_at_once) {
      EXPECT_CALL(*drain_timer_, disableTimer());
    }
    EXPECT_CALL(mock_connection_,
                close(Network::ConnectionCloseType::FlushWrite, "dynamic_module_bridge_drained"))
        .WillOnce(Invoke([this, close_at_once](Network::ConnectionCloseType, absl::string_view) {
          if (close_at_once) {
            drainer_->onEvent(Network::ConnectionEvent::LocalClose);
          }
        }));
    mock_conn_data_->release_callback_ = [this]() { released_ = true; };
  }

  // Reports the drained connection's close and runs the deletion it schedules.
  void closeDrainedConnection(Network::ConnectionEvent event) {
    EXPECT_CALL(*drain_timer_, disableTimer());
    drainer_->onEvent(event);
    mock_connection_.dispatcher_.clearDeferredDeleteList();
    EXPECT_TRUE(released_);
  }

  Envoy::Tcp::ConnectionPool::UpstreamCallbacks* drainer_ = nullptr;
  Event::MockTimer* drain_timer_ = nullptr;
  bool released_ = false;
};

// Unsent data keeps the connection out of the pool until it is written, since closing at once
// would cut the request short behind a FIN that makes it look whole. The drainer releases the
// connection only once its close is reported.
TEST_F(HttpTcpBridgeDrainTest, DestroyDrainsUnsentUpstreamDataBeforeTheClose) {
  bridge_->sendUpstreamData("request", false);
  expectDrain();
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  EXPECT_FALSE(released_);

  // A closing connection reads nothing more, but the pool's interface still needs these.
  Buffer::OwnedImpl reply("reply");
  drainer_->onUpstreamData(reply, false);
  EXPECT_EQ(0U, reply.length());
  drainer_->onAboveWriteBufferHighWatermark();
  drainer_->onBelowWriteBufferLowWatermark();
  drainer_->onEvent(Network::ConnectionEvent::Connected);
  EXPECT_FALSE(released_);

  closeDrainedConnection(Network::ConnectionEvent::RemoteClose);
}

// Only the part of the data the socket took counts as sent, so data written in two parts and taken
// in part still drains.
TEST_F(HttpTcpBridgeDrainTest, PartlySentUpstreamDataDrains) {
  bridge_->sendUpstreamData("req", false);
  bridge_->sendUpstreamData("uest", false);
  mock_connection_.raiseBytesSentCallbacks(3);
  expectDrain();
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  closeDrainedConnection(Network::ConnectionEvent::LocalClose);
}

// Data the socket has not taken would count against the next request's own unsent data, so even a
// connection whose module enabled reuse drains and closes.
TEST_F(HttpTcpBridgeDrainTest, EnabledReuseStillDrainsUnsentUpstreamData) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/test"}};
  EXPECT_OK(bridge_->encodeHeaders(headers, true));
  enableReuse(*bridge_);
  bridge_->sendUpstreamData("request", false);
  expectDrain();
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  closeDrainedConnection(Network::ConnectionEvent::LocalClose);
}

// Each write the socket takes during the drain restarts its bound, and the drainer's bytes sent
// callback stays registered while the drainer lives and unregisters once it is gone.
TEST_F(HttpTcpBridgeDrainTest, DrainProgressRestartsTheBound) {
  bridge_->sendUpstreamData("request", false);
  expectDrain();
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  ASSERT_EQ(2U, mock_connection_.bytes_sent_callbacks_.size());
  Network::Connection::BytesSentCb& drain_progress = mock_connection_.bytes_sent_callbacks_.back();
  EXPECT_CALL(*drain_timer_, enableTimer(std::chrono::milliseconds(1000), _)).Times(2);
  EXPECT_TRUE(drain_progress(3));
  EXPECT_TRUE(drain_progress(4));
  closeDrainedConnection(Network::ConnectionEvent::LocalClose);
  EXPECT_FALSE(drain_progress(1));
}

// An upstream that takes none of the unsent data in time gets a reset, so it never reads the part
// it has as a whole request.
TEST_F(HttpTcpBridgeDrainTest, DrainTimeoutResetsTheConnection) {
  bridge_->sendUpstreamData("request", false);
  expectDrain();
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_drain_timeout"))
      .WillOnce(Return());
  drain_timer_->invokeCallback();
  closeDrainedConnection(Network::ConnectionEvent::LocalClose);
}

// A drain whose close completes before it returns still deletes the drainer once.
TEST_F(HttpTcpBridgeDrainTest, DrainThatClosesAtOnceDeletesTheDrainer) {
  bridge_->sendUpstreamData("request", false);
  expectDrain(true);
  bridge_.reset();
  ASSERT_NE(nullptr, drainer_);
  mock_connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_TRUE(released_);
}

TEST_F(HttpTcpBridgeTest, WatermarksAfterResetStream) {
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  EXPECT_CALL(mock_upstream_to_downstream_, onAboveWriteBufferHighWatermark()).Times(0);
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(mock_upstream_to_downstream_, onBelowWriteBufferLowWatermark()).Times(0);
  bridge_->onBelowWriteBufferLowWatermark();
}

TEST_F(HttpTcpBridgeTest, OnUpstreamDataAfterResetStream) {
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_reset_stream"));
  bridge_->resetStream();

  Buffer::OwnedImpl data("response");
  EXPECT_CALL(mock_upstream_to_downstream_, decodeHeaders(_, _)).Times(0);
  bridge_->onUpstreamData(data, false);
}

TEST_F(HttpTcpBridgeTest, SendCallbacksAfterResetStreamAreNoOps) {
  EXPECT_CALL(mock_connection_, close(Network::ConnectionCloseType::AbortReset,
                                      "dynamic_module_bridge_reset_stream"));
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

  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createClusterOptions(
      const envoy::extensions::upstreams::http::dynamic_modules::v3::Config& config) {
    Protobuf::Any typed_config;
    std::ignore = typed_config.PackFrom(config);
    return factory_.createClusterOptions(typed_config, server_context_);
  }

  // Creates a connection pool of a cluster that has the given options.
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ProtocolOptionsConfigConstSharedPtr options) {
    ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, extensionProtocolOptions(_))
        .WillByDefault(Return(std::move(options)));
    return factory_.createGenericConnPool(
        host_, cm_.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
        Upstream::ResourcePriority::Default, std::nullopt, nullptr, createProtoConfig());
  }

  DynamicModuleGenericConnPoolFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Envoy::Tcp::ConnectionPool::MockInstance mock_pool_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
};

TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateSuccess) {
  auto options = createClusterOptions(createProtoConfig());
  ASSERT_OK(options);
  ASSERT_NE(options.value(), nullptr);
  EXPECT_NE(createGenericConnPool(options.value()), nullptr);
}

// Load the module via the ``module.local.filename`` data source instead of by name. The cluster
// options load the module without a factory context, so only the synchronous sources are supported.
TEST_F(DynamicModuleGenericConnPoolFactoryTest, CreateSuccessWithLocalFile) {
  envoy::extensions::upstreams::http::dynamic_modules::v3::Config config;
  config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c/"
                                  "libupstream_bridge_no_op.so"));
  config.set_bridge_name("test_bridge");

  auto options = createClusterOptions(config);
  ASSERT_OK(options);
  EXPECT_NE(createGenericConnPool(options.value()), nullptr);
}

// The connection pools of a cluster share the bridge configuration that the cluster created, so
// creating them loads nothing.
TEST_F(DynamicModuleGenericConnPoolFactoryTest, ConnPoolsShareTheClusterOptions) {
  auto options = createClusterOptions(createProtoConfig());
  ASSERT_OK(options);
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", "/should/not/find/this/path", 1);
  EXPECT_NE(createGenericConnPool(options.value()), nullptr);
  EXPECT_NE(createGenericConnPool(options.value()), nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, ClusterWithoutOptions) {
  EXPECT_ENVOY_BUG(EXPECT_EQ(createGenericConnPool(nullptr), nullptr),
                   "the cluster has no dynamic module bridge configuration");
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, ModuleLoadFailure) {
  auto config = createProtoConfig();
  config.mutable_dynamic_module_config()->set_name("nonexistent_module");

  EXPECT_THAT(createClusterOptions(config),
              HasStatusMessage(testing::HasSubstr("nonexistent_module")));
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, BridgeConfigCreateFailure) {
  auto config = createProtoConfig();
  config.mutable_dynamic_module_config()->set_name("upstream_bridge_config_new_fail");

  EXPECT_THAT(createClusterOptions(config),
              HasStatusMessage(
                  testing::HasSubstr("failed to initialize dynamic module bridge configuration")));
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, NoBridgeConfig) {
  auto config = createProtoConfig();
  config.clear_bridge_config();

  auto options = createClusterOptions(config);
  ASSERT_OK(options);
  EXPECT_NE(options.value(), nullptr);
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, BridgeConfigParseFailure) {
  auto config = createProtoConfig();
  config.set_bridge_name("parse_fail_test");

  auto* any = config.mutable_bridge_config();
  any->set_type_url("type.googleapis.com/google.protobuf.StringValue");
  any->set_value(std::string("\x0F\xFF\xFF", 3));

  EXPECT_THAT(createClusterOptions(config),
              HasStatusMessage(testing::HasSubstr("failed to parse bridge_config")));
}

TEST_F(DynamicModuleGenericConnPoolFactoryTest, InvalidTypedConfig) {
  Protobuf::Any typed_config;
  typed_config.set_type_url(
      "type.googleapis.com/envoy.extensions.upstreams.http.dynamic_modules.v3.Config");
  typed_config.set_value(std::string("\xFF", 1));

  EXPECT_FALSE(factory_.createClusterOptions(typed_config, server_context_).ok());
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
  std::ignore = config.mutable_bridge_config()->PackFrom(struct_value);

  auto options = createClusterOptions(config);
  ASSERT_OK(options);
  EXPECT_NE(options.value(), nullptr);
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
  auto options = createClusterOptions(createProtoConfig());
  ASSERT_OK(options);

  NiceMock<Upstream::MockClusterManager> cm2;
  cm2.initializeThreadLocalClusters({"fake_cluster"});
  EXPECT_CALL(cm2.thread_local_cluster_, tcpConnPool(_, _, _)).WillOnce(Return(std::nullopt));
  ON_CALL(*cm2.thread_local_cluster_.cluster_.info_, extensionProtocolOptions(_))
      .WillByDefault(Return(options.value()));

  auto pool = factory_.createGenericConnPool(
      host_, cm2.thread_local_cluster_, Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      Upstream::ResourcePriority::Default, std::nullopt, nullptr, createProtoConfig());
  EXPECT_EQ(pool, nullptr);
}

} // namespace
} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
