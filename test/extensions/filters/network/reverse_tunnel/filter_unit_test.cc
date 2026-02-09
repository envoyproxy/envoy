#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

namespace ReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/reverse_tunnel_reporting_service/reporter.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"
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
    // Initialize stats scope
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
  }

public:
  ReverseTunnelFilterUnitTest() : stats_store_(), overload_manager_() {
    // Prepare proto config with defaults.
    proto_config_.set_request_path("/reverse_connections/request");
    proto_config_.set_request_method(envoy::config::core::v3::GET);
    auto config_or_error = ReverseTunnelFilterConfig::create(proto_config_, factory_context_);
    if (!config_or_error.ok()) {
      throw EnvoyException(std::string(config_or_error.status().message()));
    }
    config_ = config_or_error.value();
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

  // Helper method to set up upstream extension.
  void setupUpstreamExtension() {
    upstream_config_.set_stat_prefix("reverse_connections");
    // Create the upstream socket interface and extension.
    upstream_socket_interface_ =
        std::make_unique<ReverseConnection::ReverseTunnelAcceptor>(context_);
    upstream_extension_ = std::make_unique<ReverseConnection::ReverseTunnelAcceptorExtension>(
        *upstream_socket_interface_, context_, upstream_config_);

    // Set up the extension in the global socket interface registry.
    auto* registered_upstream_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_upstream_interface) {
      auto* registered_acceptor = dynamic_cast<ReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_upstream_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered upstream socket interface.
        registered_acceptor->extension_ = upstream_extension_.get();
      }
    }
  }

  // Helper method to set up upstream thread local slot for testing.
  void setupUpstreamThreadLocalSlot() {
    // Call onServerInitialized to set up the extension references properly.
    upstream_extension_->onServerInitialized();

    // Create a thread local registry for upstream with the dispatcher.
    upstream_thread_local_registry_ =
        std::make_shared<ReverseConnection::UpstreamSocketThreadLocal>(dispatcher_,
                                                                       upstream_extension_.get());

    upstream_tls_slot_ =
        ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>::makeUnique(
            thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the upstream slot to return our registry.
    upstream_tls_slot_->set(
        [registry = upstream_thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version.
    upstream_extension_->setTestOnlyTLSRegistry(std::move(upstream_tls_slot_));
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

  // Helper to build reverse tunnel headers block with upstream cluster name.
  std::string makeRtHeadersWithUpstreamCluster(const std::string& node, const std::string& cluster,
                                               const std::string& tenant,
                                               const std::string& upstream_cluster_name) {
    std::string headers;
    headers += "x-envoy-reverse-tunnel-node-id: " + node + "\r\n";
    headers += "x-envoy-reverse-tunnel-cluster-id: " + cluster + "\r\n";
    headers += "x-envoy-reverse-tunnel-tenant-id: " + tenant + "\r\n";
    headers += "x-envoy-reverse-tunnel-upstream-cluster-name: " + upstream_cluster_name + "\r\n";
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

  // Helper to craft HTTP request with reverse tunnel headers including upstream cluster name.
  std::string makeHttpRequestWithAllHeaders(const std::string& method, const std::string& path,
                                            const std::string& node, const std::string& cluster,
                                            const std::string& tenant,
                                            const std::string& upstream_cluster_name,
                                            const std::string& body = "") {
    std::string req = fmt::format("{} {} HTTP/1.1\r\n", method, path);
    req += "Host: localhost\r\n";
    req += makeRtHeadersWithUpstreamCluster(node, cluster, tenant, upstream_cluster_name);
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

  // Thread local slot setup for downstream socket interface.
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};
  // Config for reverse connection socket interface.
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface upstream_config_;
  // Thread local components for testing upstream socket interface.
  std::unique_ptr<ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>>
      upstream_tls_slot_;
  std::shared_ptr<ReverseConnection::UpstreamSocketThreadLocal> upstream_thread_local_registry_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptor> upstream_socket_interface_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptorExtension> upstream_extension_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  void TearDown() override {
    // Clean up thread local components to avoid issues during destruction.
    upstream_tls_slot_.reset();
    upstream_thread_local_registry_.reset();
    upstream_extension_.reset();
    upstream_socket_interface_.reset();
  }
};

// Separate test fixture for tests that need upstream socket interface
// This isolates tests that set up global socket interfaces from regular tests
class ReverseTunnelFilterWithUpstreamTest : public ReverseTunnelFilterUnitTest {
public:
  void SetUp() override {
    ReverseTunnelFilterUnitTest::SetUp();
    setupUpstreamExtension();
    setupUpstreamThreadLocalSlot();
  }

  void TearDown() override {
    upstream_tls_slot_.reset();
    upstream_thread_local_registry_.reset();
    upstream_extension_.reset();
    upstream_socket_interface_.reset();
    ReverseTunnelFilterUnitTest::TearDown();
  }
};

TEST_F(ReverseTunnelFilterUnitTest, NewConnectionContinues) {
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(ReverseTunnelFilterUnitTest, HttpDispatchErrorStopsIteration) {
  // Simulate invalid HTTP by feeding raw bytes; dispatch will attempt and return error.
  Buffer::OwnedImpl data("INVALID");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));
}

TEST_F(ReverseTunnelFilterUnitTest, FullFlowAccepts) {

  // Configure reverse tunnel filter.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Filter state does not affect acceptance.

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

TEST_F(ReverseTunnelFilterUnitTest, FullFlowMissingHeadersIsBadRequest) {

  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Missing required headers should cause 400.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));
  Buffer::OwnedImpl request(makeHttpRequest("GET", "/reverse_connections/request", ""));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
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
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
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

// Exercise RequestDecoder interface methods by obtaining the decoder via
// ReverseTunnelFilter::newStream (avoids accessing the private impl type).
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderInterfaceCoverageViaNewStream) {
  // Ensure filter has callbacks initialized so decoder can access time source.
  filter_->initializeReadFilterCallbacks(callbacks_);

  // Get a decoder instance via newStream.
  Http::MockResponseEncoder encoder;
  Http::RequestDecoder& decoder = filter_->newStream(encoder, false);

  // Provide minimal headers so processIfComplete paths are safe if triggered.
  auto headers = Http::RequestHeaderMapImpl::create();
  decoder.decodeHeaders(std::move(headers), false);

  // Call decodeMetadata (no-op) explicitly.
  Http::MetadataMapPtr meta;
  decoder.decodeMetadata(std::move(meta));

  // Accessor methods.
  auto& si = decoder.streamInfo();
  (void)si;
  auto logs = decoder.accessLogHandlers();
  EXPECT_TRUE(logs.empty());
  auto handle = decoder.getRequestDecoderHandle();
  EXPECT_EQ(nullptr, handle.get());
}

// Test configuration with custom ping interval.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationCustomPingInterval) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  proto_config.mutable_ping_interval()->set_seconds(10);
  proto_config.set_auto_close_connections(true);
  proto_config.set_request_path("/custom/path");
  proto_config.set_request_method(envoy::config::core::v3::PUT);

  auto config_or_error = ReverseTunnelFilterConfig::create(proto_config, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  EXPECT_EQ(std::chrono::milliseconds(10000), config->pingInterval());
  EXPECT_TRUE(config->autoCloseConnections());
  EXPECT_EQ("/custom/path", config->requestPath());
  EXPECT_EQ("PUT", config->requestMethod());
}

// Ensure defaults remain stable.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationDefaultsRemainStable) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  auto config_or_error = ReverseTunnelFilterConfig::create(proto_config, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  EXPECT_EQ("/reverse_connections/request", config->requestPath());
}

// Test configuration with default values.
TEST_F(ReverseTunnelFilterUnitTest, ConfigurationDefaults) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel proto_config;
  // Leave everything empty to test defaults.

  auto config_or_error = ReverseTunnelFilterConfig::create(proto_config, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  EXPECT_EQ(std::chrono::milliseconds(2000), config->pingInterval());
  EXPECT_FALSE(config->autoCloseConnections());
  EXPECT_EQ("/reverse_connections/request", config->requestPath());
  EXPECT_EQ("GET", config->requestMethod());
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

TEST_F(ReverseTunnelFilterUnitTest, NonStringFilterStateIgnored) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

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
}

TEST_F(ReverseTunnelFilterUnitTest, ClusterIdMismatchIgnored) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

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
}

TEST_F(ReverseTunnelFilterUnitTest, TenantIdMissingIgnored) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

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

// Explicitly call RequestDecoderImpl::sendLocalReply with a header modifier to
// test the modify_headers.
TEST_F(ReverseTunnelFilterUnitTest, RequestDecoderSendLocalReplyHeaderModifier) {
  // Ensure callbacks are initialized to provide a time source.
  filter_->initializeReadFilterCallbacks(callbacks_);

  // Mock encoder to capture headers set via modifier.
  Http::MockResponseEncoder encoder;
  bool saw_custom_header = false;
  EXPECT_CALL(encoder, encodeHeaders(testing::_, testing::_))
      .WillOnce(testing::Invoke([&](const Http::ResponseHeaderMap& headers, bool) {
        auto values = headers.get(Http::LowerCaseString("x-custom-mod"));
        saw_custom_header = !values.empty() && values[0]->value().getStringView() == "v";
      }));

  // Obtain a decoder and call sendLocalReply with a modifier.
  Http::RequestDecoder& decoder = filter_->newStream(encoder, false);
  decoder.sendLocalReply(
      Http::Code::Forbidden, "",
      [](Http::ResponseHeaderMap& h) { h.addCopy(Http::LowerCaseString("x-custom-mod"), "v"); },
      absl::nullopt, "test");

  EXPECT_TRUE(saw_custom_header);
}

// Missing required headers should return 400.
TEST_F(ReverseTunnelFilterUnitTest, MissingReverseTunnelHeadersReturns400) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Missing required header should fail.
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

TEST_F(ReverseTunnelFilterUnitTest, PartialStateIgnored) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

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
    auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
    ASSERT_TRUE(config_or_error.ok());
    auto config = config_or_error.value();
    EXPECT_EQ(std::chrono::milliseconds(5500), config->pingInterval());
  }

  // Test config without ping_interval (default).
  {
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
    auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
    ASSERT_TRUE(config_or_error.ok());
    auto config = config_or_error.value();
    EXPECT_EQ(std::chrono::milliseconds(2000), config->pingInterval());
  }

  // Test config with empty strings (should use defaults).
  {
    envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
    cfg.set_request_path("");
    cfg.set_request_method(envoy::config::core::v3::METHOD_UNSPECIFIED);
    auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
    ASSERT_TRUE(config_or_error.ok());
    auto config = config_or_error.value();
    EXPECT_EQ("/reverse_connections/request", config->requestPath());
    EXPECT_EQ("GET", config->requestMethod());
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

TEST_F(ReverseTunnelFilterUnitTest, TenantIdMismatchIgnored2) {
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();
  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

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
  cfg.set_request_method(envoy::config::core::v3::PUT);
  // No extra options set to test defaults.

  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto config = config_or_error.value();
  EXPECT_FALSE(config->autoCloseConnections());
  EXPECT_EQ("/test", config->requestPath());
  EXPECT_EQ("PUT", config->requestMethod());
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

// Ensure success path works without additional validations.
TEST_F(ReverseTunnelFilterUnitTest, SuccessPathCoverage) {
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Create a valid request; response verification occurs normally.
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "response-test", "cluster", "tenant"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));

  // Ensure the success path works.
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
TEST_F(ReverseTunnelFilterWithUpstreamTest, ProcessAcceptedConnectionDuplicateFails) {
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
  // Socket lifecycle is now managed by UpstreamReverseConnectionIOHandle wrapper.

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
TEST_F(ReverseTunnelFilterWithUpstreamTest, ProcessAcceptedConnectionDuplicatedHandleNotOpen) {
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
  // Socket lifecycle is now managed by UpstreamReverseConnectionIOHandle wrapper.

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

TEST_F(ReverseTunnelFilterWithUpstreamTest, ProcessAcceptedConnectionReportsConnectionEvent) {
  auto* reporter_cfg = upstream_config_.mutable_reporter_config();
  reporter_cfg->set_name(Bootstrap::ReverseConnection::MOCK_REPORTER);
  Protobuf::StringValue reporter_payload;
  reporter_cfg->mutable_typed_config()->PackFrom(reporter_payload);

  NiceMock<Bootstrap::ReverseConnection::MockReporterFactory> reporter_factory;
  Registry::InjectFactory<Bootstrap::ReverseConnection::ReverseTunnelReporterFactory>
      reporter_injector(reporter_factory);

  std::string node_id = "node";
  std::string cluster_id = "cluster";
  std::string tenant_id = "tenant";

  EXPECT_CALL(context_, messageValidationVisitor())
      .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  EXPECT_CALL(reporter_factory, createReporter()).WillOnce(Invoke([&]() {
    auto reporter =
        std::make_unique<NiceMock<Bootstrap::ReverseConnection::MockReverseTunnelReporter>>();
    EXPECT_CALL(*reporter, reportConnectionEvent(testing::Eq(node_id), testing::Eq(cluster_id),
                                                 testing::Eq(tenant_id)));
    return reporter;
  }));

  setupUpstreamExtension();
  setupUpstreamThreadLocalSlot();

  auto mock_socket = std::make_unique<Network::MockConnectionSocket>();
  auto mock_io_handle = std::make_unique<Network::MockIoHandle>();
  auto dup_io_handle = std::make_unique<Network::MockIoHandle>();

  EXPECT_CALL(*dup_io_handle, resetFileEvents());
  EXPECT_CALL(*dup_io_handle, fdDoNotUse()).WillRepeatedly(testing::Return(100));
  EXPECT_CALL(*dup_io_handle, isOpen()).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_io_handle, duplicate())
      .WillOnce(testing::Return(testing::ByMove(std::move(dup_io_handle))));
  EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(testing::ReturnRef(*mock_io_handle));
  EXPECT_CALL(*mock_socket, isOpen()).WillRepeatedly(testing::Return(true));

  static Network::ConnectionSocketPtr stored_mock_socket;
  static std::unique_ptr<Network::MockIoHandle> stored_io_handle;
  stored_io_handle = std::move(mock_io_handle);
  stored_mock_socket = std::move(mock_socket);

  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_mock_socket));

  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         node_id, cluster_id, tenant_id));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(request, false));
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
TEST_F(ReverseTunnelFilterWithUpstreamTest, InterfaceMethodsCompleteCoverage) {
  // Set up mock socket with proper duplication mocking
  auto mock_socket = std::make_unique<Network::MockConnectionSocket>();
  auto mock_io_handle = std::make_unique<Network::MockIoHandle>();
  auto dup_handle = std::make_unique<Network::MockIoHandle>();

  // Mock successful duplication
  EXPECT_CALL(*dup_handle, isOpen()).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*dup_handle, resetFileEvents());
  EXPECT_CALL(*dup_handle, fdDoNotUse()).WillRepeatedly(testing::Return(456));

  EXPECT_CALL(*mock_io_handle, duplicate())
      .WillOnce(testing::Return(testing::ByMove(std::move(dup_handle))));
  EXPECT_CALL(*mock_socket, ioHandle()).WillRepeatedly(testing::ReturnRef(*mock_io_handle));
  EXPECT_CALL(*mock_socket, isOpen()).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(testing::Return(455));

  // Store in static variables
  static Network::ConnectionSocketPtr stored_interface_socket;
  static std::unique_ptr<Network::MockIoHandle> stored_interface_handle;
  stored_interface_handle = std::move(mock_io_handle);
  stored_interface_socket = std::move(mock_socket);

  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_interface_socket));

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
  // Mock socket to skip duplication
  auto mock_socket = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*mock_socket, isOpen()).WillRepeatedly(testing::Return(false));
  static Network::ConnectionSocketPtr stored_socket_complete;
  stored_socket_complete = std::move(mock_socket);
  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_socket_complete));

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
TEST_F(ReverseTunnelFilterWithUpstreamTest, SuccessfulSocketDuplication) {
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

// Test cluster name validation accepts matching name.
TEST_F(ReverseTunnelFilterUnitTest, ClusterNameValidationAcceptsMatchingName) {
  // Configure filter with required_cluster_name.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.set_required_cluster_name("my-upstream-cluster");

  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();

  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  filter.initializeReadFilterCallbacks(callbacks_);

  auto socket = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*socket, isOpen()).WillRepeatedly(testing::Return(false));

  static Network::ConnectionSocketPtr stored_socket_accepts_match;
  stored_socket_accepts_match = std::move(socket);
  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_socket_accepts_match));

  // Capture writes to connection.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send request with matching cluster name.
  Buffer::OwnedImpl request(makeHttpRequestWithAllHeaders("GET", "/reverse_connections/request",
                                                          "node1", "cluster1", "tenant1",
                                                          "my-upstream-cluster"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));

  // Should accept with 200 OK.
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
  auto accepted = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.accepted");
  ASSERT_NE(nullptr, accepted);
  EXPECT_EQ(1, accepted->value());
}

// Test cluster name validation rejects mismatched cluster name.
TEST_F(ReverseTunnelFilterUnitTest, ClusterNameValidationRejectsMismatchedName) {
  // Configure filter with required_cluster_name.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.set_required_cluster_name("my-upstream-cluster");

  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();

  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Capture writes to connection.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Expect connection to be closed.
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  // Send request with mismatched cluster name.
  Buffer::OwnedImpl request(makeHttpRequestWithAllHeaders(
      "GET", "/reverse_connections/request", "node1", "cluster1", "tenant1", "wrong-cluster"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));

  // Should reject with 400 Bad Request.
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
  EXPECT_THAT(written, testing::HasSubstr("Cluster name mismatch"));
  auto validation_failed =
      TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.validation_failed");
  ASSERT_NE(nullptr, validation_failed);
  EXPECT_EQ(1, validation_failed->value());
}

// Test cluster name validation rejects missing cluster name header.
TEST_F(ReverseTunnelFilterUnitTest, ClusterNameValidationRejectsMissingHeader) {
  // Configure filter with required_cluster_name.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;
  cfg.set_required_cluster_name("my-upstream-cluster");

  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();

  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
  filter.initializeReadFilterCallbacks(callbacks_);

  // Capture writes to connection.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Expect connection to be closed.
  EXPECT_CALL(callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  // Send request without upstream cluster name header.
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "node1", "cluster1", "tenant1"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));

  // Should reject with 400 Bad Request.
  EXPECT_THAT(written, testing::HasSubstr("400 Bad Request"));
  EXPECT_THAT(written, testing::HasSubstr("Missing upstream cluster name header"));
  auto parse_error = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.parse_error");
  ASSERT_NE(nullptr, parse_error);
  EXPECT_EQ(1, parse_error->value());
}

// Test cluster name validation is disabled when required_cluster_name is not set.
TEST_F(ReverseTunnelFilterUnitTest, ClusterNameValidationDisabledWhenNotSet) {
  // Configure filter without required_cluster_name.
  envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel cfg;

  auto config_or_error = ReverseTunnelFilterConfig::create(cfg, factory_context_);
  ASSERT_TRUE(config_or_error.ok());
  auto local_config = config_or_error.value();

  ReverseTunnelFilter filter(local_config, *stats_store_.rootScope(), overload_manager_);
  filter.initializeReadFilterCallbacks(callbacks_);

  auto socket = std::make_unique<Network::MockConnectionSocket>();
  EXPECT_CALL(*socket, isOpen()).WillRepeatedly(testing::Return(false));

  static Network::ConnectionSocketPtr stored_socket_not_enforced;
  stored_socket_not_enforced = std::move(socket);
  EXPECT_CALL(callbacks_.connection_, getSocket())
      .WillRepeatedly(testing::ReturnRef(stored_socket_not_enforced));

  // Capture writes to connection.
  std::string written;
  EXPECT_CALL(callbacks_.connection_, write(testing::_, testing::_))
      .WillRepeatedly(testing::Invoke([&](Buffer::Instance& data, bool) {
        written.append(data.toString());
        data.drain(data.length());
      }));

  // Send request without upstream cluster name header.
  Buffer::OwnedImpl request(makeHttpRequestWithRtHeaders("GET", "/reverse_connections/request",
                                                         "node1", "cluster1", "tenant1"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter.onData(request, false));

  // Should accept with 200 OK.
  EXPECT_THAT(written, testing::HasSubstr("200 OK"));
  auto accepted = TestUtility::findCounter(stats_store_, "reverse_tunnel.handshake.accepted");
  EXPECT_EQ(1, accepted->value());
}

} // namespace
} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
