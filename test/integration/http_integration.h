#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "source/common/http/codec_client.h"
#include "source/common/network/filter_impl.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/printers.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#endif

namespace Envoy {

using ::Envoy::Http::Http2::Http2Frame;

enum class Http2Impl {
  Nghttp2,
  Oghttp2,
};

/**
 * HTTP codec client used during integration testing.
 */
class IntegrationCodecClient : public Http::CodecClientProd {
public:
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                         Network::ClientConnectionPtr&& conn,
                         Upstream::HostDescriptionConstSharedPtr host_description,
                         Http::CodecType type);
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                         Network::ClientConnectionPtr&& conn,
                         Upstream::HostDescriptionConstSharedPtr host_description,
                         Http::CodecType type, bool wait_till_connected);

  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(const Http::RequestHeaderMap& headers);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                                  uint64_t body_size, bool end_stream = true);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                                  const std::string& body, bool end_stream = true);
  bool sawGoAway() const { return saw_goaway_; }
  bool connected() const { return connected_; }
  bool streamOpen() const { return !stream_gone_; }
  void sendData(Http::RequestEncoder& encoder, absl::string_view data, bool end_stream);
  void sendData(Http::RequestEncoder& encoder, Buffer::Instance& data, bool end_stream);
  void sendData(Http::RequestEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::RequestEncoder& encoder, const Http::RequestTrailerMap& trailers);
  void sendReset(Http::RequestEncoder& encoder);
  // Intentionally makes a copy of metadata_map.
  void sendMetadata(Http::RequestEncoder& encoder, Http::MetadataMap metadata_map);
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr>
  startRequest(const Http::RequestHeaderMap& headers, bool header_only_request = false);
  ABSL_MUST_USE_RESULT AssertionResult
  waitForDisconnect(std::chrono::milliseconds time_to_wait = TestUtility::DefaultTimeout);
  Network::ClientConnection* connection() const { return connection_.get(); }
  Network::ConnectionEvent lastConnectionEvent() const { return last_connection_event_; }
  Network::Connection& rawConnection() { return *connection_; }
  bool disconnected() { return disconnected_; }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationCodecClient& parent, bool block_till_connected)
        : parent_(parent), block_till_connected_(block_till_connected) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationCodecClient& parent_;
    bool block_till_connected_;
  };

  struct CodecCallbacks : public Http::ConnectionCallbacks {
    CodecCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::ConnectionCallbacks
    void onGoAway(Http::GoAwayErrorCode) override { parent_.saw_goaway_ = true; }

    IntegrationCodecClient& parent_;
  };

  struct CodecClientCallbacks : public Http::CodecClientCallbacks {
    CodecClientCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::CodecClientCallbacks
    void onStreamDestroy() override { parent_.stream_gone_ = true; }
    void onStreamReset(Http::StreamResetReason) override { parent_.stream_gone_ = true; }

    IntegrationCodecClient& parent_;
  };

  void flushWrite();

  Event::Dispatcher& dispatcher_;
  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  CodecClientCallbacks codec_client_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
  bool stream_gone_{};
  Network::ConnectionEvent last_connection_event_;
};

using IntegrationCodecClientPtr = std::unique_ptr<IntegrationCodecClient>;

/**
 * Test fixture for HTTP and HTTP/2 integration tests.
 */
class HttpIntegrationTest : public BaseIntegrationTest {
public:
  HttpIntegrationTest(Http::CodecType downstream_protocol, Network::Address::IpVersion version)
      : HttpIntegrationTest(
            downstream_protocol, version,
            ConfigHelper::httpProxyConfig(/*downstream_use_quic=*/downstream_protocol ==
                                          Http::CodecType::HTTP3)) {}
  HttpIntegrationTest(Http::CodecType downstream_protocol, Network::Address::IpVersion version,
                      const std::string& config);

  HttpIntegrationTest(Http::CodecType downstream_protocol,
                      const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version)
      : HttpIntegrationTest(
            downstream_protocol, upstream_address_fn, version,
            ConfigHelper::httpProxyConfig(/*downstream_use_quic=*/downstream_protocol ==
                                          Http::CodecType::HTTP3)) {}
  HttpIntegrationTest(Http::CodecType downstream_protocol,
                      const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version, const std::string& config);
  ~HttpIntegrationTest() override;

  void initialize() override;
  void setupHttp2Overrides(Http2Impl implementation);

protected:
  void useAccessLog(absl::string_view format = "",
                    std::vector<envoy::config::core::v3::TypedExtensionConfig> formatters = {});

  IntegrationCodecClientPtr makeHttpConnection(uint32_t port);
  // Makes a http connection object without checking its connected state.
  virtual IntegrationCodecClientPtr makeRawHttpConnection(
      Network::ClientConnectionPtr&& conn,
      absl::optional<envoy::config::core::v3::Http2ProtocolOptions> http2_options);
  // Makes a downstream network connection object based on client codec version.
  Network::ClientConnectionPtr makeClientConnectionWithOptions(
      uint32_t port, const Network::ConnectionSocket::OptionsSharedPtr& options) override;
  // Makes a http connection object with asserting a connected state.
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn);

  // Sets downstream_protocol_ and alters the HTTP connection manager codec type in the
  // config_helper_.
  void setDownstreamProtocol(Http::CodecType type);

  // Enable the encoding/decoding of Http1 trailers downstream
  ConfigHelper::HttpModifierFunction setEnableDownstreamTrailersHttp1();

  // Enable the encoding/decoding of Http1 trailers upstream
  ConfigHelper::ConfigModifierFunction setEnableUpstreamTrailersHttp1();

  // Enable Proxy-Status response header.
  ConfigHelper::HttpModifierFunction configureProxyStatus();

  // Sends |request_headers| and |request_body_size| bytes of body upstream.
  // Configured upstream to send |response_headers| and |response_body_size|
  // bytes of body downstream.
  // Waits |time| ms for both the request to be proxied upstream and the
  // response to be proxied downstream.
  //
  // Waits for the complete downstream response before returning.
  // Requires |codec_client_| to be initialized.
  IntegrationStreamDecoderPtr sendRequestAndWaitForResponse(
      const Http::TestRequestHeaderMapImpl& request_headers, uint32_t request_body_size,
      const Http::TestResponseHeaderMapImpl& response_headers, uint32_t response_body_size,
      uint64_t upstream_index = 0, std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  IntegrationStreamDecoderPtr sendRequestAndWaitForResponse(
      const Http::TestRequestHeaderMapImpl& request_headers, uint32_t request_body_size,
      const Http::TestResponseHeaderMapImpl& response_headers, uint32_t response_body_size,
      const std::vector<uint64_t>& upstream_indices,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Wait for the end of stream on the next upstream stream on any of the provided fake upstreams.
  // Sets fake_upstream_connection_ to the connection and upstream_request_ to stream.
  // In cases where the upstream that will receive the request is not deterministic, a second
  // upstream index may be provided, in which case both upstreams will be checked for requests.
  absl::optional<uint64_t> waitForNextUpstreamRequest(
      const std::vector<uint64_t>& upstream_indices,
      std::chrono::milliseconds connection_wait_timeout = TestUtility::DefaultTimeout);
  void waitForNextUpstreamRequest(
      uint64_t upstream_index = 0,
      std::chrono::milliseconds connection_wait_timeout = TestUtility::DefaultTimeout);

  absl::optional<uint64_t>
  waitForNextUpstreamConnection(const std::vector<uint64_t>& upstream_indices,
                                std::chrono::milliseconds connection_wait_timeout,
                                FakeHttpConnectionPtr& fake_upstream_connection);

  // Close |codec_client_| and |fake_upstream_connection_| cleanly.
  void cleanupUpstreamAndDownstream();

  // Verifies the response_headers contains the expected_headers, and response body matches given
  // body string.
  void verifyResponse(IntegrationStreamDecoderPtr response, const std::string& response_code,
                      const Http::TestResponseHeaderMapImpl& expected_headers,
                      const std::string& expected_body);

  // Helper that sends a request to Envoy, and verifies if Envoy response headers and body size is
  // the same as the expected headers map.
  // Requires the "http" port has been registered.
  void sendRequestAndVerifyResponse(const Http::TestRequestHeaderMapImpl& request_headers,
                                    const int request_size,
                                    const Http::TestResponseHeaderMapImpl& response_headers,
                                    const int response_size, const int backend_idx,
                                    absl::optional<const Http::TestResponseHeaderMapImpl>
                                        expected_response_headers = absl::nullopt);

  // Check for completion of upstream_request_, and a simple "200" response.
  void checkSimpleRequestSuccess(uint64_t expected_request_size, uint64_t expected_response_size,
                                 IntegrationStreamDecoder* response);

  using ConnectionCreationFunction = std::function<Network::ClientConnectionPtr()>;
  // Sends a simple header-only HTTP request, and waits for a response.
  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(ConnectionCreationFunction* create_connection,
                                                    int upstream_index,
                                                    const std::string& path = "/test/long/url",
                                                    const std::string& overwrite_authority = "");
  void testRouterNotFound();
  void testRouterNotFoundWithBody();
  void testRouterVirtualClusters();
  void testRouteStats();
  void testRouterUpstreamProtocolError(const std::string&, const std::string&);

  void testRouterRequestAndResponseWithBody(
      uint64_t request_size, uint64_t response_size, bool big_header,
      bool set_content_length_header = false, ConnectionCreationFunction* creator = nullptr,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  void testRouterHeaderOnlyRequestAndResponse(ConnectionCreationFunction* creator = nullptr,
                                              int upstream_index = 0,
                                              const std::string& path = "/test/long/url",
                                              const std::string& overwrite_authority = "");

  // Disconnect tests
  void testRouterUpstreamDisconnectBeforeRequestComplete();
  void
  testRouterUpstreamDisconnectBeforeResponseComplete(ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeRequestComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeResponseComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterUpstreamResponseBeforeRequestComplete();

  void testTwoRequests(bool force_network_backup = false);
  void testLargeHeaders(Http::TestRequestHeaderMapImpl request_headers,
                        Http::TestRequestTrailerMapImpl request_trailers, uint32_t size,
                        uint32_t max_size);
  void testLargeRequestUrl(uint32_t url_size, uint32_t max_headers_size);
  void testLargeRequestHeaders(uint32_t size, uint32_t count, uint32_t max_size = 60,
                               uint32_t max_count = 100,
                               std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);
  void testLargeRequestTrailers(uint32_t size, uint32_t max_size = 60);
  void testManyRequestHeaders(std::chrono::milliseconds time = TestUtility::DefaultTimeout);

  void testAddEncodedTrailers();
  void testRetry();
  void testRetryHittingBufferLimit();
  void testRetryAttemptCountHeader();
  void testGrpcRetry();

  void testEnvoyHandling1xx(bool additional_continue_from_upstream = false,
                            const std::string& via = "", bool disconnect_after_100 = false);
  void testEnvoyProxying1xx(bool continue_before_upstream_complete = false,
                            bool with_encoder_filter = false,
                            bool with_multiple_1xx_headers = false,
                            absl::string_view initial_code = "100");
  void simultaneousRequest(uint32_t request1_bytes, uint32_t request2_bytes,
                           uint32_t response1_bytes, uint32_t response2_bytes);

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  // Test that trailers are sent. request_trailers_present and
  // response_trailers_present will check if the trailers are present, otherwise
  // makes sure they were dropped.
  void testTrailers(uint64_t request_size, uint64_t response_size, bool request_trailers_present,
                    bool response_trailers_present);
  // Test /drain_listener from admin portal.
  void testAdminDrain(Http::CodecClient::Type admin_request_type);

  // Test sending and receiving large request and response bodies with autonomous upstream.
  void testGiantRequestAndResponse(
      uint64_t request_size, uint64_t response_size, bool set_content_length_header,
      std::chrono::milliseconds timeout = 2 * TestUtility::DefaultTimeout * TSAN_TIMEOUT_FACTOR);

  struct BytesCountExpectation {
    BytesCountExpectation(int wire_bytes_sent, int wire_bytes_received, int header_bytes_sent,
                          int header_bytes_received)
        : wire_bytes_sent_{wire_bytes_sent}, wire_bytes_received_{wire_bytes_received},
          header_bytes_sent_{header_bytes_sent}, header_bytes_received_{header_bytes_received} {}
    int wire_bytes_sent_;
    int wire_bytes_received_;
    int header_bytes_sent_;
    int header_bytes_received_;
  };

  void expectUpstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                          BytesCountExpectation h2_expectation,
                                          BytesCountExpectation h3_expectation, const int id = 0);

  void expectDownstreamBytesSentAndReceived(BytesCountExpectation h1_expectation,
                                            BytesCountExpectation h2_expectation,
                                            BytesCountExpectation h3_expectation, const int id = 0);

  Http::CodecClient::Type downstreamProtocol() const { return downstream_protocol_; }
  std::string downstreamProtocolStatsRoot() const;
  // Return the upstream protocol part of the stats root.
  std::string upstreamProtocolStatsRoot() const;
  // Prefix listener stat with IP:port, including IP version dependent loopback address.
  std::string listenerStatPrefix(const std::string& stat_name);

  Network::UpstreamTransportSocketFactoryPtr quic_transport_socket_factory_;
  // Must outlive |codec_client_| because it may not close connection till the end of its life
  // scope.
  std::unique_ptr<Http::PersistentQuicInfo> quic_connection_persistent_info_;
  // The client making requests to Envoy.
  IntegrationCodecClientPtr codec_client_;
  // A placeholder for the first upstream connection.
  FakeHttpConnectionPtr fake_upstream_connection_;
  // A placeholder for the first request received at upstream.
  FakeStreamPtr upstream_request_;
  // A pointer to the request encoder, if used.
  Http::RequestEncoder* request_encoder_{nullptr};
  // The response headers sent by sendRequestAndWaitForResponse() by default.
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestRequestHeaderMapImpl default_request_headers_{{":method", "GET"},
                                                          {":path", "/test/long/url"},
                                                          {":scheme", "http"},
                                                          {":authority", "sni.lyft.com"}};
  // The codec type for the client-to-Envoy connection
  Http::CodecType downstream_protocol_{Http::CodecType::HTTP1};
  std::string access_log_name_;
  testing::NiceMock<Random::MockRandomGenerator> random_;
  Quic::QuicStatNames quic_stat_names_;
  std::string san_to_match_{"spiffe://lyft.com/backend-team"};
  bool enable_quic_early_data_{true};
#ifdef ENVOY_ENABLE_QUIC
  quic::DeterministicConnectionIdGenerator connection_id_generator_{
      quic::kQuicDefaultConnectionIdLength};
#endif
};

// Helper class for integration tests using raw HTTP/2 frames
class Http2RawFrameIntegrationTest : public HttpIntegrationTest {
public:
  Http2RawFrameIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecType::HTTP2, version) {}

protected:
  void startHttp2Session();
  Http2Frame readFrame();
  void sendFrame(const Http2Frame& frame);
  virtual void beginSession();

  IntegrationTcpClientPtr tcp_client_;
};

} // namespace Envoy
