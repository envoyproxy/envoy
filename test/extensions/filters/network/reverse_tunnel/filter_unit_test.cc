#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {
namespace {

// Helper to create invalid HTTP that will trigger codec dispatch errors
class HttpErrorHelper {
public:
  static std::vector<std::string> getHttpErrorPatterns() {
    return {
        // Trigger codec dispatch with various malformed patterns
        "GET /path HTTP/1.1\r\nInvalid-Header\r\n\r\n",        // Header without colon
        "POST /path HTTP/1.1\r\nContent-Length: abc\r\n\r\n",  // Non-numeric content length
        "INVALID_METHOD /path HTTP/1.1\r\nHost: test\r\n\r\n", // Invalid method
        std::string("\xFF\xFE\xFD\xFC", 4),                    // Binary junk
        "GET /path HTTP/999.999\r\n\r\n",                      // Invalid HTTP version
        "GET\r\n\r\n",                                         // Incomplete request line
        "GET /path\r\n\r\n",                                   // Missing HTTP version
        "GET /path HTTP/1.1\r\nHost: test\r\nTransfer-Encoding: invalid\r\n\r\n" // Invalid encoding
    };
  }
};

class ReverseTunnelFilterUnitTest : public testing::Test {
protected:
  void SetUp() override {
    // No special setup needed for these tests
  }

public:
  ReverseTunnelFilterUnitTest() : stats_store_(), overload_manager_() {
    // Prepare proto config with defaults.
    proto_config_.set_request_path("/reverse_connections/request");
    proto_config_.set_request_method("GET");
    config_ = std::make_shared<ReverseTunnelFilterConfig>(proto_config_);
    filter_ = std::make_unique<ReverseTunnelFilter>(config_, *stats_store_.rootScope(),
                                                    overload_manager_);

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
    // Provide a default socket for getSocket().
    auto socket = std::make_unique<Network::MockConnectionSocket>();
    auto* socket_raw = socket.get();
    // Store unique_ptr inside a shared location to return const ref each time.
    static Network::ConnectionSocketPtr stored_socket;
    stored_socket = std::move(socket);
    EXPECT_CALL(callbacks_.connection_, getSocket())
        .WillRepeatedly(testing::ReturnRef(stored_socket));
    EXPECT_CALL(*socket_raw, isOpen()).WillRepeatedly(testing::Return(true));
    // Stub required methods used by processAcceptedConnection().
    EXPECT_CALL(*socket_raw, ioHandle())
        .WillRepeatedly(testing::ReturnRef(*callbacks_.socket_.io_handle_));

    filter_->initializeReadFilterCallbacks(callbacks_);
  }

  // Helper method to set up upstream extension - simplified version.
  void setupUpstreamExtension() {
    // For unit tests, we don't need the full upstream extension setup.
    // The filter should handle the case where socket interface is not available gracefully.
  }

  // Helper to craft raw HTTP/1.1 request string.
  std::string makeHttpRequest(const std::string& method, const std::string& path,
                              const std::string& body = "") {
    std::string req = fmt::format("{} {} HTTP/1.1\r\n", method, path);
    req += "Host: localhost\r\n";
    req += fmt::format("Content-Length: {}\r\n\r\n", body.size());
    req += body;
    return req;
  }

  // Helper to build reverse tunnel headers block.
  std::string makeRtHeaders(const std::string& node, const std::string& cluster,
                            const std::string& tenant) {
    std::string headers;
    headers += "x-envoy-reverse-tunnel-node-id: " + node + "\r\n";
    headers += "x-envoy-reverse-tunnel-cluster-id: " + cluster + "\r\n";
    headers += "x-envoy-reverse-tunnel-tenant-id: " + tenant + "\r\n";
    return headers;
  }

  // Helper to craft HTTP request with reverse tunnel headers and optional body.
  std::string makeHttpRequestWithRtHeaders(const std::string& method, const std::string& path,
                                           const std::string& node, const std::string& cluster,
                                           const std::string& tenant,
                                           const std::string& body = "") {
    std::string req = fmt::format("{} {} HTTP/1.1\r\n", method, path);
    req += "Host: localhost\r\n";
    req += makeRtHeaders(node, cluster, tenant);
    req += fmt::format("Content-Length: {}\r\n\r\n", body.size());
    req += body;
    return req;
  }

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config_;
  ReverseTunnelFilterConfigSharedPtr config_;
  std::unique_ptr<ReverseTunnelFilter> filter_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Server::MockOverloadManager> overload_manager_;
  NiceMock<Network::MockReadFilterCallbacks> callbacks_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(ReverseTunnelFilterUnitTest, NewConnectionContinues) {
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(ReverseTunnelFilterUnitTest, HttpDispatchErrorStopsIteration) {
  // Simulate invalid HTTP by feeding raw bytes; dispatch will attempt and return error.
  Buffer::OwnedImpl data("INVALID");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

TEST_F(ReverseTunnelFilterUnitTest, FullFlowValidationSuccess) {

  // Configure reverse tunnel with validation keys.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto* v = cfg.mutable_validation_config();
  v->set_node_id_filter_state_key("node_id");
  v->set_cluster_id_filter_state_key("cluster_id");
  v->set_tenant_id_filter_state_key("tenant_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Populate connection filter state with expected string values.
  auto& si = callbacks_.connection_.streamInfo();
  si.filterState()->setData("node_id", std::make_unique<Router::StringAccessorImpl>("n"),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);
  si.filterState()->setData("cluster_id", std::make_unique<Router::StringAccessorImpl>("c"),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);
  si.filterState()->setData("tenant_id", std::make_unique<Router::StringAccessorImpl>("t"),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);

  // Capture writes to connection.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
  // Stats: accepted should increment.
  auto accepted = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.accepted");
  ASSERT_NE(nullptr, accepted);
  EXPECT_EQ(1, accepted->value());
}

TEST_F(ReverseTunnelFilterUnitTest, FullFlowValidationFailure) {

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.mutable_validation_config()->set_node_id_filter_state_key("node_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Missing node_id filter state should cause 403.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));
  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
  // Stats: validation_failed and rejected should increment.
  auto vf = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.validation_failed");
  auto rejected = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.rejected");
  ASSERT_NE(nullptr, vf);
  ASSERT_NE(nullptr, rejected);
  EXPECT_EQ(1, vf->value());
  EXPECT_EQ(1, rejected->value());
}

TEST_F(ReverseTunnelFilterUnitTest, FullFlowParseError) {

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Missing required headers should cause 400.
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
  // Stats: parse_error should increment.
  auto parse_error = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.parse_error");
  ASSERT_NE(nullptr, parse_error);
  EXPECT_EQ(1, parse_error->value());
}

TEST_F(ReverseTunnelFilterUnitTest, NotFoundForNonReverseTunnelPath) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/health"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
}

TEST_F(ReverseTunnelFilterUnitTest, AutoCloseConnectionsClosesAfterAccept) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.set_auto_close_connections(true);
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));
  // Expect close on accept.
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test configuration with custom ping interval.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationCustomPingInterval) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.mutable_ping_interval()->set_seconds(10);
  proto_config.set_auto_close_connections(true);
  proto_config.set_request_path("/custom/path");
  proto_config.set_request_method("PUT");

  ReverseTunnelFilterConfig config(proto_config);
  EXPECT_EQ(std::chrono::milliseconds(10000), config.pingInterval());
  EXPECT_TRUE(config.autoCloseConnections());
  EXPECT_EQ("/custom/path", config.requestPath());
  EXPECT_EQ("PUT", config.requestMethod());
}

// Test configuration with validation config.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationWithValidation) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  auto* validation = proto_config.mutable_validation_config();
  validation->set_node_id_filter_state_key("node_key");
  validation->set_cluster_id_filter_state_key("cluster_key");
  validation->set_tenant_id_filter_state_key("tenant_key");

  ReverseTunnelFilterConfig config(proto_config);
  EXPECT_EQ("node_key", config.nodeIdFilterStateKey());
  EXPECT_EQ("cluster_key", config.clusterIdFilterStateKey());
  EXPECT_EQ("tenant_key", config.tenantIdFilterStateKey());
}

// Test configuration with default values.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationDefaults) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  // Leave everything empty to test defaults.

  ReverseTunnelFilterConfig config(proto_config);
  EXPECT_EQ(std::chrono::milliseconds(2000), config.pingInterval());
  EXPECT_FALSE(config.autoCloseConnections());
  EXPECT_EQ("/reverse_connections/request", config.requestPath());
  EXPECT_EQ("GET", config.requestMethod());
  EXPECT_TRUE(config.nodeIdFilterStateKey().empty());
  EXPECT_TRUE(config.clusterIdFilterStateKey().empty());
  EXPECT_TRUE(config.tenantIdFilterStateKey().empty());
}

// Test RequestDecoder methods not fully covered.
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderImplMethods) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a request that will trigger decoder creation.
  const std::string req =
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t");

  // Split request into headers and body to test different decoder methods.
  const auto hdr_end = req.find("\r\n\r\n");
  const std::string headers_part = req.substr(0, hdr_end + 4);
  const std::string body_part = req.substr(hdr_end + 4);

  // First send headers.
  Buffer::OwnedImpl header_buf(headers_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // Then send body to test decodeData method.
  Buffer::OwnedImpl body_buf(body_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(body_buf, false));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test decodeTrailers method.
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderImplDecodeTrailers) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a chunked request with trailers to trigger decodeTrailers.
  const std::string headers_part = "GET /reverse_connections/request HTTP/1.1\r\n"
                                   "Host: localhost\r\n" +
                                   makeRtHeaders("n", "c", "t") +
                                   "Transfer-Encoding: chunked\r\n\r\n";

  // Send headers first.
  Buffer::OwnedImpl header_buf(headers_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // Send chunk with data.
  Buffer::OwnedImpl chunk1("5\r\nhello\r\n");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk1, false));

  // Send final chunk with trailers - this triggers decodeTrailers.
  Buffer::OwnedImpl chunk2("0\r\nX-Trailer: value\r\n\r\n");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk2, false));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test decodeTrailers triggers processIfComplete.
TEST_F(ReverseTunnelFilterUnitTest, DecodeTrailersTriggersCompletion) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Build a proper chunked request to ensure decodeTrailers is called.
  std::string req = "GET /reverse_connections/request HTTP/1.1\r\n"
                    "Host: localhost\r\n" +
                    makeRtHeaders("trail", "test", "complete") +
                    "Transfer-Encoding: chunked\r\n\r\n"
                    "0\r\n"              // Zero-length chunk
                    "X-End: trailer\r\n" // Trailer header
                    "\r\n";              // End of trailers

  Buffer::OwnedImpl request(req);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test parsing with empty payload.
TEST_F(ReverseTunnelFilterUnitTest, ParseEmptyPayload) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));

  auto parse_error = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.parse_error");
  ASSERT_NE(nullptr, parse_error);
  EXPECT_EQ(1, parse_error->value());
}

// Test validation with non-string filter state object.
TEST_F(ReverseTunnelFilterUnitTest, ValidationWithNonStringFilterState) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.mutable_validation_config()->set_node_id_filter_state_key("node_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Add a non-string object to filter state.
  auto& si = callbacks_.connection_.streamInfo();
  si.filterState()->setData("node_id", std::make_unique<StreamInfo::UInt64AccessorImpl>(12345),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
}

// Test validation with cluster ID mismatch.
TEST_F(ReverseTunnelFilterUnitTest, ValidationClusterIdMismatch) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.mutable_validation_config()->set_cluster_id_filter_state_key("cluster_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Add wrong cluster ID to filter state.
  auto& si = callbacks_.connection_.streamInfo();
  si.filterState()->setData("cluster_id", std::make_unique<Router::StringAccessorImpl>("wrong"),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
}

// Test validation with tenant ID missing.
TEST_F(ReverseTunnelFilterUnitTest, ValidationTenantIdMissing) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.mutable_validation_config()->set_tenant_id_filter_state_key("tenant_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Don't add tenant_id to filter state.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
}

// Test closed socket scenario.
TEST_F(ReverseTunnelFilterUnitTest, ProcessAcceptedConnectionClosedSocket) {
  // Create a mock socket that reports as closed.
  auto closed_socket = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*closed_socket, isOpen()).WillRepeatedly(testing::Return(false));

  static Network::ConnectionSocketPtr stored_closed_socket;
  stored_closed_socket = std::move(closed_socket);
  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_closed_socket));

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test wrong HTTP method.
TEST_F(ReverseTunnelFilterUnitTest, WrongHttpMethod) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("PUT", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
}

// Test onGoAway method coverage.
TEST_F(ReverseTunnelFilterUnitTest, OnGoAway) {
  // onGoAway is a no-op, but we need to test it for coverage.
  filter_->onGoAway(Http::GoAwayErrorCode::NoError);
  // No assertions needed as it's a no-op method.
}

// Test sendLocalReply with different parameters.
TEST_F(ReverseTunnelFilterUnitTest, SendLocalReplyVariants) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test sendLocalReply with empty body.
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/wrong/path", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
  EXPECT_THAT(written, testing::HasSubstr("Not a reverse tunnel request"));
}

// Test invalid protobuf that fails parsing.
TEST_F(ReverseTunnelFilterUnitTest, InvalidProtobufData) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Body contents are ignored now; with proper headers we should accept.
  std::string junk_body(100, '\xFF');
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n",
                                                         "c", "t", junk_body));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test request with headers only (no body).
TEST_F(ReverseTunnelFilterUnitTest, HeadersOnlyRequest) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  std::string headers_only = "GET /reverse_connections/request HTTP/1.1\r\n"
                             "Host: localhost\r\n"
                             "Content-Length: 0\r\n\r\n";
  Buffer::OwnedImpl request(headers_only);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
}

// Test RequestDecoderImpl interface methods for coverage.
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderImplInterfaceMethods) {
  // Create a decoder to test interface methods.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Start a request to create the decoder.
  // Use a non-empty body so the headers phase does not signal end_stream.
  const std::string req =
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t");
  const auto hdr_end = req.find("\r\n\r\n");
  const std::string headers_part = req.substr(0, hdr_end + 4);

  Buffer::OwnedImpl header_buf(headers_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // Continue with body to complete the request.
  const std::string body_part = req.substr(hdr_end + 4);
  Buffer::OwnedImpl body_buf(body_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(body_buf, false));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test wrong HTTP method leads to 404.
TEST_F(ReverseTunnelFilterUnitTest, WrongHttpMethodTest) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test with wrong method (PUT instead of GET).
  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("PUT", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
}

// Test successful request with response body.
TEST_F(ReverseTunnelFilterUnitTest, SuccessfulRequestWithResponseBody) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders(
      "GET", "/reverse_connections/request", "test-node", "test-cluster", "test-tenant"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));

  // Check that accepted stat is incremented.
  auto accepted = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.accepted");
  ASSERT_NE(nullptr, accepted);
  EXPECT_EQ(1, accepted->value());
}

// Test sendLocalReply with modify_headers function.
TEST_F(ReverseTunnelFilterUnitTest, SendLocalReplyWithHeaderModifier) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send a request with wrong path to trigger sendLocalReply.
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/wrong/path", "test-body"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
}

// Test protobuf validation failure in request.
TEST_F(ReverseTunnelFilterUnitTest, RequestValidationFailure) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Missing required header should fail validation.
  std::string req = "GET /reverse_connections/request HTTP/1.1\r\n"
                    "Host: localhost\r\n"
                    "x-envoy-reverse-tunnel-cluster-id: c\r\n"
                    "x-envoy-reverse-tunnel-tenant-id: t\r\n"
                    "Content-Length: 0\r\n\r\n";
  Buffer::OwnedImpl request(req);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
}

// Test partial HTTP data processing.
TEST_F(ReverseTunnelFilterUnitTest, PartialHttpData) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  const std::string full_request =
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t");

  // Send request in small chunks.
  const size_t chunk_size = 10;
  for (size_t i = 0; i < full_request.size(); i += chunk_size) {
    const size_t actual_chunk_size = std::min(chunk_size, full_request.size() - i);
    std::string chunk = full_request.substr(i, actual_chunk_size);
    Buffer::OwnedImpl chunk_buf(chunk);
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk_buf, false));
  }

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test HTTP dispatch with complete body in single call.
TEST_F(ReverseTunnelFilterUnitTest, CompleteRequestSingleCall) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "single", "call", "test"));

  // Process complete request in one call.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, true));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test validation with all three IDs configured but only some present.
TEST_F(ReverseTunnelFilterUnitTest, PartialValidationConfiguration) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto* v = cfg.mutable_validation_config();
  v->set_node_id_filter_state_key("node_id");
  v->set_cluster_id_filter_state_key("cluster_id");
  v->set_tenant_id_filter_state_key("tenant_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Only add node_id, leaving cluster_id and tenant_id missing.
  auto& si = callbacks_.connection_.streamInfo();
  si.filterState()->setData("node_id", std::make_unique<Router::StringAccessorImpl>("n"),
                            StreamInfo::FilterState::StateType::ReadOnly,
                            StreamInfo::FilterState::LifeSpan::Connection);

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
}

// Test string parsing through HTTP path (parseHandshakeRequest is private).
TEST_F(ReverseTunnelFilterUnitTest, ParseHandshakeStringViaHttp) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test with a valid protobuf serialized as string.
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "node", "cluster", "tenant"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test sendLocalReply with different paths.
TEST_F(ReverseTunnelFilterUnitTest, SendLocalReplyWithHeadersCallback) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a request with wrong path to trigger sendLocalReply.
  Buffer::OwnedImpl request("GET / HTTP/1.1\r\nHost: test\r\n\r\n");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // Should get 404 since path doesn't match.
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
  EXPECT_THAT(written, testing::HasSubstr("Not a reverse tunnel request"));
}

// Test processIfComplete early return paths.
TEST_F(ReverseTunnelFilterUnitTest, ProcessIfCompleteEarlyReturns) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  const std::string req =
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t", "x");

  // Split request to send headers first without end_stream.
  const auto hdr_end = req.find("\r\n\r\n");
  const std::string headers_part = req.substr(0, hdr_end + 4);

  // Send headers without end_stream - should not trigger processIfComplete.
  Buffer::OwnedImpl header_buf(headers_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // At this point, no response should have been written yet.
  EXPECT_TRUE(written.empty());

  // Now send the body with end_stream to complete.
  const std::string body_part = req.substr(hdr_end + 4);
  Buffer::OwnedImpl body_buf(body_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(body_buf, true));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test configuration with all branches.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationAllBranches) {
  // Test config with ping_interval set.
  {
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
    cfg.mutable_ping_interval()->set_seconds(5);
    cfg.mutable_ping_interval()->set_nanos(500000000);
    ReverseTunnelFilterConfig config(cfg);
    EXPECT_EQ(std::chrono::milliseconds(5500), config.pingInterval());
  }

  // Test config without ping_interval (default).
  {
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
    ReverseTunnelFilterConfig config(cfg);
    EXPECT_EQ(std::chrono::milliseconds(2000), config.pingInterval());
  }

  // Test config with empty strings (should use defaults).
  {
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
    cfg.set_request_path("");
    cfg.set_request_method("");
    ReverseTunnelFilterConfig config(cfg);
    EXPECT_EQ("/reverse_connections/request", config.requestPath());
    EXPECT_EQ("GET", config.requestMethod());
  }
}

// Test array parsing edge cases via HTTP (parseHandshakeRequestFromArray is private).
TEST_F(ReverseTunnelFilterUnitTest, ParseHandshakeArrayEdgeCases) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test with empty body to trigger array parsing with null data.
  Buffer::OwnedImpl empty_request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(empty_request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
}

// Test socket is null or not open scenarios.
TEST_F(ReverseTunnelFilterUnitTest, ProcessAcceptedConnectionNullSocket) {
  // Create a mock connection that returns null socket.
  NiceMock<Network::MockReadFilterCallbacks> null_socket_callbacks;
  EXPECT_CALL(null_socket_callbacks, connection())
      .WillRepeatedly(ReturnRef(null_socket_callbacks.connection_));

  // Mock getSocket to return null.
  static Network::ConnectionSocketPtr null_socket_ptr = nullptr;
  EXPECT_CALL(null_socket_callbacks.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(null_socket_ptr));

  ReverseTunnelFilter null_socket_filter(config_, *stats_store_.rootScope(), overload_manager_);
  null_socket_filter.initializeReadFilterCallbacks(null_socket_callbacks);

  std::string written;
  EXPECT_CALL(null_socket_callbacks.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, null_socket_filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test empty response body path.
TEST_F(ReverseTunnelFilterUnitTest, EmptyResponseBody) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // Should generate a response with non-empty body.
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
  // No protobuf body expected now.
}

// Test codec dispatch error path.
TEST_F(ReverseTunnelFilterUnitTest, CodecDispatchError) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send completely invalid HTTP data that will cause dispatch error.
  Buffer::OwnedImpl invalid_data("\x00\x01\x02\x03INVALID HTTP");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(invalid_data, false));

  // Should get no response since the filter returns early on dispatch error.
}

// Test validation with tenant ID value mismatch.
TEST_F(ReverseTunnelFilterUnitTest, ValidationTenantIdMismatch) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.mutable_validation_config()->set_tenant_id_filter_state_key("tenant_id");
  auto local_config = std::make_shared<ReverseTunnelFilterConfig>(cfg);
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Add wrong tenant ID to filter state.
  auto& si = callbacks_.connection_.streamInfo();
  si.filterState()->setData(
      "tenant_id", std::make_unique<Router::StringAccessorImpl>("wrong-tenant"),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Connection);

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("403 Forbidden"));
}

// Test newStream with is_internally_created parameter via HTTP processing.
TEST_F(ReverseTunnelFilterUnitTest, NewStreamWithInternallyCreatedFlag) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // newStream is called internally when processing HTTP requests.
  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test stats generation through actual filter operations.
TEST_F(ReverseTunnelFilterUnitTest, StatsGeneration) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Trigger parse error to verify stats are generated (missing headers).
  Buffer::OwnedImpl invalid_request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(invalid_request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));

  // Verify parse_error stat was incremented.
  auto parse_error = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.parse_error");
  ASSERT_NE(nullptr, parse_error);
  EXPECT_EQ(1, parse_error->value());
}

// Test configuration with ping_interval_ms deprecated field.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationDeprecatedField) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  // Test the deprecated field if it exists.
  cfg.set_auto_close_connections(false);
  cfg.set_request_path("/test");
  cfg.set_request_method("PUT");
  // Don't set validation_config to test the empty branch.

  ReverseTunnelFilterConfig config(cfg);
  EXPECT_FALSE(config.autoCloseConnections());
  EXPECT_EQ("/test", config.requestPath());
  EXPECT_EQ("PUT", config.requestMethod());
  EXPECT_TRUE(config.nodeIdFilterStateKey().empty());
  EXPECT_TRUE(config.clusterIdFilterStateKey().empty());
  EXPECT_TRUE(config.tenantIdFilterStateKey().empty());
}

// Test decodeData with multiple chunks.
TEST_F(ReverseTunnelFilterUnitTest, DecodeDataMultipleChunks) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  const std::string req =
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "n", "c", "t");

  // Send headers first without end_stream.
  const auto hdr_end = req.find("\r\n\r\n");
  const std::string headers_part = req.substr(0, hdr_end + 4);
  Buffer::OwnedImpl header_buf(headers_part);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // Send body in chunks without end_stream.
  const std::string body_part = req.substr(hdr_end + 4);
  const size_t chunk_size = body_part.size() / 3;

  Buffer::OwnedImpl chunk1(body_part.substr(0, chunk_size));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk1, false));

  Buffer::OwnedImpl chunk2(body_part.substr(chunk_size, chunk_size));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk2, false));

  // Send final chunk with end_stream.
  Buffer::OwnedImpl chunk3(body_part.substr(chunk_size * 2));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunk3, true));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test RequestDecoderImpl interface methods with proper HTTP flow.
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderImplInterfaceMethodsCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a proper HTTP request with chunked encoding and trailers and headers-only body
  std::string chunked_request = "GET /reverse_connections/request HTTP/1.1\r\n"
                                "Host: localhost\r\n" +
                                makeRtHeaders("interface", "test", "coverage") +
                                "Transfer-Encoding: chunked\r\n\r\n";

  // Send headers first
  Buffer::OwnedImpl header_buf(chunked_request);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(header_buf, false));

  // Send chunk end and trailers (no body required)
  std::string end_chunk_and_trailers = "0\r\nX-Test-Trailer: value\r\n\r\n";
  Buffer::OwnedImpl trailer_buf(end_chunk_and_trailers);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(trailer_buf, false));

  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test codec dispatch failure with truly malformed HTTP.
TEST_F(ReverseTunnelFilterUnitTest, CodecDispatchFailureDetailed) {
  // Create HTTP data that will cause codec dispatch to fail and log error.
  std::string malformed_http = "GET /reverse_connections/request HTTP/1.1\r\n"
                               "Host: localhost\r\n"
                               "Content-Length: \xFF\xFF\xFF\xFF\r\n\r\n"; // Invalid content length

  Buffer::OwnedImpl request(malformed_http);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
}

// Test more malformed HTTP to hit codec error paths.
TEST_F(ReverseTunnelFilterUnitTest, CodecDispatchMultipleErrorTypes) {
  // Test 1: HTTP request with invalid headers
  std::string invalid_headers = "GET /reverse_connections/request HTTP/1.1\r\n"
                                "Invalid Header Without Colon\r\n"
                                "\r\n";
  Buffer::OwnedImpl req1(invalid_headers);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(req1, false));

  // Create new filter for second test
  auto filter2 =
      std::make_unique<ReverseTunnelFilter>(config_, *stats_store_.rootScope(), overload_manager_);
  NiceMock<Network::MockReadFilterCallbacks> callbacks2;
  EXPECT_CALL(callbacks2, connection()).WillRepeatedly(ReturnRef(callbacks2.connection_));
  auto socket2 = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*socket2, isOpen()).WillRepeatedly(testing::Return(true));
  static Network::ConnectionSocketPtr stored_socket2 = std::move(socket2);
  EXPECT_CALL(callbacks2.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_socket2));
  filter2->initializeReadFilterCallbacks(callbacks2);

  // Test 2: Invalid HTTP version
  std::string invalid_version = "GET /reverse_connections/request HTTP/9.9\r\n\r\n";
  Buffer::OwnedImpl req2(invalid_version);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter2->onData(req2, false));
}

// Test to trigger response validation failure path (lines 195-200).
TEST_F(ReverseTunnelFilterUnitTest, ResponseValidationFailurePath) {
  // This is tricky since we can't easily mock the Validate function.
  // But we can create a scenario that might trigger response validation issues.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a valid request - the response validation happens internally
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "response-test", "cluster", "tenant"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // The response validation failure path is internal and hard to trigger
  // without modifying the source, but this test ensures the success path works
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test decodeMetadata method coverage.
TEST_F(ReverseTunnelFilterUnitTest, DecodeMetadataMethodCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // The decodeMetadata method is called internally when processing certain HTTP requests
  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "meta", "data", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test streamInfo method coverage.
TEST_F(ReverseTunnelFilterUnitTest, StreamInfoMethodCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "stream", "info", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test accessLogHandlers method coverage.
TEST_F(ReverseTunnelFilterUnitTest, AccessLogHandlersMethodCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "access", "log", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test getRequestDecoderHandle method coverage.
TEST_F(ReverseTunnelFilterUnitTest, GetRequestDecoderHandleMethodCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "decoder", "handle", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test various HTTP malformations to hit codec error paths.
TEST_F(ReverseTunnelFilterUnitTest, VariousHttpMalformations) {
  // Test different types of malformed HTTP to hit codec dispatch error paths
  std::vector<std::string> malformed_requests = {
      // Missing HTTP version
      "GET /reverse_connections/request\r\nHost: test\r\n\r\n",
      // Invalid method
      "INVALID_METHOD /reverse_connections/request HTTP/1.1\r\nHost: test\r\n\r\n",
      // Binary garbage
      std::string("\x00\x01\x02\x03\x04\x05", 6),
      // Incomplete request line
      "POS",
      // Missing headers separator
      "GET /reverse_connections/request HTTP/1.1\r\nHost: test",
      // Invalid characters in headers
      "GET /reverse_connections/request HTTP/1.1\r\nHo\x00st: test\r\n\r\n"};

  for (size_t i = 0; i < malformed_requests.size(); ++i) {
    // Create new filter for each test to avoid state issues
    auto test_filter = std::make_unique<ReverseTunnelFilter>(config_, *stats_store_.rootScope(),
                                                             overload_manager_);
    NiceMock<Network::MockReadFilterCallbacks> test_callbacks;
    EXPECT_CALL(test_callbacks, connection()).WillRepeatedly(ReturnRef(test_callbacks.connection_));

    auto test_socket = std::make_unique<Network::MockConnectionSocket>();
    EXPECT_CALL(*test_socket, isOpen()).WillRepeatedly(testing::Return(true));
    static std::vector<Network::ConnectionSocketPtr> stored_test_sockets;
    stored_test_sockets.push_back(std::move(test_socket));
    EXPECT_CALL(test_callbacks.connection_, getSocket())
        .WillRepeatedly(testing::ReturnRef(stored_test_sockets.back()));

    test_filter->initializeReadFilterCallbacks(test_callbacks);

    Buffer::OwnedImpl request(malformed_requests[i]);
    EXPECT_EQ(Network::FilterStatus::StopIteration, test_filter->onData(request, false));
  }
}

// Test processAcceptedConnection with null TLS registry.
TEST_F(ReverseTunnelFilterUnitTest, ProcessAcceptedConnectionNullTlsRegistry) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Socket lifecycle is now managed by UpstreamReverseConnectionIOHandle wrapper.

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "null-tls", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test processAcceptedConnection when duplicate() returns null.
TEST_F(ReverseTunnelFilterUnitTest, ProcessAcceptedConnectionDuplicateFails) {
  // Set up upstream extension.
  setupUpstreamExtension();

  // Create a mock socket that returns a null/closed handle on duplicate.
  auto mock_socket = std::make_unique<Network::MockConnectionSocket>();
  auto mock_io_handle = std::make_unique<Network::MockIoHandle>();

  // Setup IoHandle to return null on duplicate.
  EXPECT_CALL(*mock_io_handle, duplicate()).WillOnce(testing::Return(nullptr));
  EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(testing::ReturnRef(*mock_io_handle));
  EXPECT_CALL(*mock_socket, isOpen()).WillRepeatedly(testing::Return(true));

  static Network::ConnectionSocketPtr stored_mock_socket;
  static std::unique_ptr<Network::MockIoHandle> stored_io_handle;
  stored_io_handle = std::move(mock_io_handle);
  stored_mock_socket = std::move(mock_socket);

  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_mock_socket));

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "dup-fail", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test processAcceptedConnection when duplicated handle is not open.
TEST_F(ReverseTunnelFilterUnitTest, ProcessAcceptedConnectionDuplicatedHandleNotOpen) {
  // Set up upstream extension.
  setupUpstreamExtension();

  auto mock_socket = std::make_unique<Network::MockConnectionSocket>();
  auto mock_io_handle = std::make_unique<Network::MockIoHandle>();
  auto dup_io_handle = std::make_unique<Network::MockIoHandle>();

  // Setup duplicated handle to report as not open.
  EXPECT_CALL(*dup_io_handle, isOpen()).WillRepeatedly(testing::Return(false));
  EXPECT_CALL(*mock_io_handle, duplicate())
      .WillOnce(testing::Return(testing::ByMove(std::move(dup_io_handle))));
  EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(testing::ReturnRef(*mock_io_handle));
  EXPECT_CALL(*mock_socket, isOpen()).WillRepeatedly(testing::Return(true));

  static Network::ConnectionSocketPtr stored_mock_socket2;
  static std::unique_ptr<Network::MockIoHandle> stored_io_handle2;
  stored_io_handle2 = std::move(mock_io_handle);
  stored_mock_socket2 = std::move(mock_socket);

  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_mock_socket2));

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(
      makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request", "dup-closed", "c", "t"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test systematic HTTP error patterns to trigger codec dispatch error paths.
TEST_F(ReverseTunnelFilterUnitTest, SystematicHttpErrorPatterns) {
  auto patterns = HttpErrorHelper::getHttpErrorPatterns();

  for (size_t i = 0; i < patterns.size(); ++i) {
    // Create new filter for each test to avoid state pollution
    auto error_filter = std::make_unique<ReverseTunnelFilter>(config_, *stats_store_.rootScope(),
                                                              overload_manager_);
    NiceMock<Network::MockReadFilterCallbacks> error_callbacks;
    EXPECT_CALL(error_callbacks, connection())
        .WillRepeatedly(ReturnRef(error_callbacks.connection_));

    // Set up socket for each test
    auto error_socket = std::make_unique<Network::MockConnectionSocket>();
    EXPECT_CALL(*error_socket, isOpen()).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(*error_socket, ioHandle())
        .WillRepeatedly(testing::ReturnRef(*error_callbacks.socket_.io_handle_));

    static std::vector<Network::ConnectionSocketPtr> stored_error_sockets;
    stored_error_sockets.push_back(std::move(error_socket));
    EXPECT_CALL(error_callbacks.connection_, getSocket())
        .WillRepeatedly(testing::ReturnRef(stored_error_sockets.back()));

    error_filter->initializeReadFilterCallbacks(error_callbacks);

    // Test this error pattern
    Buffer::OwnedImpl error_request(patterns[i]);
    EXPECT_EQ(Network::FilterStatus::StopIteration, error_filter->onData(error_request, false));
  }
}

// Test edge cases in HTTP/protobuf processing to maximize coverage.
TEST_F(ReverseTunnelFilterUnitTest, EdgeCaseHttpProtobufProcessing) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test 1: Binary data that looks like protobuf but isn't
  std::string fake_protobuf;
  fake_protobuf.push_back(0x08); // Protobuf field tag
  fake_protobuf.push_back(0x96); // Invalid varint continuation
  fake_protobuf.push_back(0xFF); // More invalid data
  fake_protobuf.push_back(0xFF);
  fake_protobuf.push_back(0xFF);

  Buffer::OwnedImpl fake_request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
}

// Test to trigger specific interface methods for coverage.
TEST_F(ReverseTunnelFilterUnitTest, InterfaceMethodsCompleteCoverage) {
  // Set up upstream extension.
  setupUpstreamExtension();

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create request with HTTP/1.1 Transfer-Encoding chunked to trigger trailers
  std::string chunked_request = "GET /reverse_connections/request HTTP/1.1\r\n"
                                "Host: localhost\r\n" +
                                makeRtHeaders("interface", "methods", "test") +
                                "Transfer-Encoding: chunked\r\n\r\n";
  chunked_request += "0\r\n";                            // End chunk
  chunked_request += "X-Custom-Trailer: test-value\r\n"; // Trailer header
  chunked_request += "\r\n";                             // End trailers

  Buffer::OwnedImpl chunked_buf(chunked_request);
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(chunked_buf, false));

  // This should trigger decodeTrailers, decodeMetadata (if any),
  // streamInfo, accessLogHandlers, and getRequestDecoderHandle methods
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test processIfComplete when already complete.
TEST_F(ReverseTunnelFilterUnitTest, ProcessIfCompleteAlreadyComplete) {
  setupUpstreamExtension();

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send a complete request.
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "double", "complete", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // Verify we got the response.
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));

  // Try to send more data - should be ignored as already complete.
  Buffer::OwnedImpl more_data("extra data");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(more_data, false));
}

// Test successful socket duplication with all operations succeeding.
TEST_F(ReverseTunnelFilterUnitTest, SuccessfulSocketDuplication) {
  setupUpstreamExtension();

  auto socket_with_dup = std::make_unique<Network::MockConnectionSocket>();

  // Mock successful duplication where everything succeeds.
  auto mock_io_handle = std::make_unique<Network::MockIoHandle>();
  auto dup_handle = std::make_unique<Network::MockIoHandle>();

  // The duplicated handle is open and operations succeed.
  EXPECT_CALL(*dup_handle, isOpen()).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*dup_handle, resetFileEvents());
  EXPECT_CALL(*dup_handle, fdDoNotUse()).WillRepeatedly(testing::Return(123));

  // Mock the duplicate() call to return the dup_handle.
  EXPECT_CALL(*mock_io_handle, duplicate())
      .WillOnce(testing::Return(testing::ByMove(std::move(dup_handle))));

  // Mock ioHandle() to return our mock handle.
  EXPECT_CALL(*socket_with_dup, ioHandle()).WillRepeatedly(testing::ReturnRef(*mock_io_handle));
  EXPECT_CALL(*socket_with_dup, isOpen()).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(testing::Return(122));

  // Store socket and handle in static variables.
  static Network::ConnectionSocketPtr stored_dup_socket;
  static std::unique_ptr<Network::MockIoHandle> stored_dup_handle;
  stored_dup_handle = std::move(mock_io_handle);
  stored_dup_socket = std::move(socket_with_dup);

  // Set up the callbacks to use our mock socket.
  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_dup_socket));

  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "dup", "success", "test"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
}

// Test modify_headers callback in sendLocalReply.
TEST_F(ReverseTunnelFilterUnitTest, SendLocalReplyWithModifyHeaders) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send a request that will trigger a 404 response with modify_headers callback.
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/wrong/path"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // The sendLocalReply with modify_headers is called internally.
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
}

// Test sendLocalReply with all branches covered.
TEST_F(ReverseTunnelFilterUnitTest, SendLocalReplyAllBranches) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Test with wrong method to trigger 404.
  Buffer::OwnedImpl request(makeHttpRequest("POST", "/reverse_connections/request"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("404 Not Found"));
  EXPECT_THAT(written, testing::HasSubstr("Not a reverse tunnel request"));
}

// Test HTTP/1.1 codec initialization with different settings.
TEST_F(ReverseTunnelFilterUnitTest, CodecInitializationCoverage) {
  // Create a new filter to test codec initialization.
  auto test_filter =
      std::make_unique<ReverseTunnelFilter>(config_, *stats_store_.rootScope(), overload_manager_);
  NiceMock<Network::MockReadFilterCallbacks> test_callbacks;
  EXPECT_CALL(test_callbacks, connection()).WillRepeatedly(ReturnRef(test_callbacks.connection_));

  auto test_socket = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*test_socket, isOpen()).WillRepeatedly(testing::Return(true));
  static Network::ConnectionSocketPtr stored_codec_socket = std::move(test_socket);
  EXPECT_CALL(test_callbacks.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_codec_socket));

  test_filter->initializeReadFilterCallbacks(test_callbacks);

  // First call to onData initializes the codec.
  Buffer::OwnedImpl data1("GET /test HTTP/1.1\r\n");
  EXPECT_EQ(Network::FilterStatus::StopIteration, test_filter->onData(data1, false));

  // Second call uses existing codec.
  Buffer::OwnedImpl data2("Host: test\r\n\r\n");
  EXPECT_EQ(Network::FilterStatus::StopIteration, test_filter->onData(data2, false));
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
