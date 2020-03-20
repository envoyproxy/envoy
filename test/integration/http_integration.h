#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "common/http/codec_client.h"
#include "common/network/filter_impl.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/printers.h"

namespace Envoy {

/**
 * HTTP codec client used during integration testing.
 */
class IntegrationCodecClient : public Http::CodecClientProd {
public:
  IntegrationCodecClient(Event::Dispatcher& dispatcher, Network::ClientConnectionPtr&& conn,
                         Upstream::HostDescriptionConstSharedPtr host_description,
                         Http::CodecClient::Type type);

  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(const Http::RequestHeaderMap& headers);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                                  uint64_t body_size);
  IntegrationStreamDecoderPtr makeRequestWithBody(const Http::RequestHeaderMap& headers,
                                                  const std::string& body);
  bool sawGoAway() const { return saw_goaway_; }
  bool connected() const { return connected_; }
  void sendData(Http::RequestEncoder& encoder, absl::string_view data, bool end_stream);
  void sendData(Http::RequestEncoder& encoder, Buffer::Instance& data, bool end_stream);
  void sendData(Http::RequestEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::RequestEncoder& encoder, const Http::RequestTrailerMap& trailers);
  void sendReset(Http::RequestEncoder& encoder);
  // Intentionally makes a copy of metadata_map.
  void sendMetadata(Http::RequestEncoder& encoder, Http::MetadataMap metadata_map);
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr>
  startRequest(const Http::RequestHeaderMap& headers);
  bool waitForDisconnect(std::chrono::milliseconds time_to_wait = std::chrono::milliseconds(0));
  Network::ClientConnection* connection() const { return connection_.get(); }
  Network::ConnectionEvent last_connection_event() const { return last_connection_event_; }
  Network::Connection& rawConnection() { return *connection_; }
  bool disconnected() { return disconnected_; }

private:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    IntegrationCodecClient& parent_;
  };

  struct CodecCallbacks : public Http::ConnectionCallbacks {
    CodecCallbacks(IntegrationCodecClient& parent) : parent_(parent) {}

    // Http::ConnectionCallbacks
    void onGoAway() override { parent_.saw_goaway_ = true; }

    IntegrationCodecClient& parent_;
  };

  void flushWrite();

  Event::Dispatcher& dispatcher_;
  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
  Network::ConnectionEvent last_connection_event_;
};

using IntegrationCodecClientPtr = std::unique_ptr<IntegrationCodecClient>;

/**
 * Test fixture for HTTP and HTTP/2 integration tests.
 */
class HttpIntegrationTest : public BaseIntegrationTest {
public:
  // TODO(jmarantz): Remove this once
  // https://github.com/envoyproxy/envoy-filter-example/pull/69 is reverted.
  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      Network::Address::IpVersion version, TestTimeSystemPtr,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG)
      : HttpIntegrationTest(downstream_protocol, version, config) {}

  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);

  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      const InstanceConstSharedPtrFn& upstream_address_fn,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);
  ~HttpIntegrationTest() override;

  // Waits for the first access log entry.
  std::string waitForAccessLog(const std::string& filename);

protected:
  void useAccessLog(absl::string_view format = "");

  IntegrationCodecClientPtr makeHttpConnection(uint32_t port);
  // Makes a http connection object without checking its connected state.
  virtual IntegrationCodecClientPtr makeRawHttpConnection(Network::ClientConnectionPtr&& conn);
  // Makes a http connection object with asserting a connected state.
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn);

  // Sets downstream_protocol_ and alters the HTTP connection manager codec type in the
  // config_helper_.
  void setDownstreamProtocol(Http::CodecClient::Type type);

  // Enable the encoding/decoding of Http1 trailers downstream
  ConfigHelper::HttpModifierFunction setEnableDownstreamTrailersHttp1();

  // Enable the encoding/decoding of Http1 trailers upstream
  ConfigHelper::ConfigModifierFunction setEnableUpstreamTrailersHttp1();

  // Sends |request_headers| and |request_body_size| bytes of body upstream.
  // Configured upstream to send |response_headers| and |response_body_size|
  // bytes of body downstream.
  //
  // Waits for the complete downstream response before returning.
  // Requires |codec_client_| to be initialized.
  IntegrationStreamDecoderPtr sendRequestAndWaitForResponse(
      const Http::TestRequestHeaderMapImpl& request_headers, uint32_t request_body_size,
      const Http::TestResponseHeaderMapImpl& response_headers, uint32_t response_body_size,
      int upstream_index = 0, std::chrono::milliseconds time = TestUtility::DefaultTimeout);

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
                                    const int response_size, const int backend_idx);

  // Check for completion of upstream_request_, and a simple "200" response.
  void checkSimpleRequestSuccess(uint64_t expected_request_size, uint64_t expected_response_size,
                                 IntegrationStreamDecoder* response);

  using ConnectionCreationFunction = std::function<Network::ClientConnectionPtr()>;
  // Sends a simple header-only HTTP request, and waits for a response.
  IntegrationStreamDecoderPtr makeHeaderOnlyRequest(ConnectionCreationFunction* create_connection,
                                                    int upstream_index,
                                                    const std::string& path = "/test/long/url",
                                                    const std::string& authority = "host");
  void testRouterNotFound();
  void testRouterNotFoundWithBody();

  void testRouterRequestAndResponseWithBody(uint64_t request_size, uint64_t response_size,
                                            bool big_header,
                                            ConnectionCreationFunction* creator = nullptr);
  void testRouterHeaderOnlyRequestAndResponse(ConnectionCreationFunction* creator = nullptr,
                                              int upstream_index = 0,
                                              const std::string& path = "/test/long/url",
                                              const std::string& authority = "host");
  void testRequestAndResponseShutdownWithActiveConnection();

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
  void testLargeRequestHeaders(uint32_t size, uint32_t count, uint32_t max_size = 60,
                               uint32_t max_count = 100);
  void testLargeRequestTrailers(uint32_t size, uint32_t max_size = 60);
  void testManyRequestHeaders(std::chrono::milliseconds time = TestUtility::DefaultTimeout);

  void testAddEncodedTrailers();
  void testRetry();
  void testRetryHittingBufferLimit();
  void testRetryAttemptCountHeader();
  void testGrpcRetry();

  void testEnvoyHandling100Continue(bool additional_continue_from_upstream = false,
                                    const std::string& via = "");
  void testEnvoyProxying100Continue(bool continue_before_upstream_complete = false,
                                    bool with_encoder_filter = false);

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  // Test that trailers are sent. request_trailers_present and
  // response_trailers_present will check if the trailers are present, otherwise
  // makes sure they were dropped.
  void testTrailers(uint64_t request_size, uint64_t response_size, bool request_trailers_present,
                    bool response_trailers_present);
  // Test /drain_listener from admin portal.
  void testAdminDrain(Http::CodecClient::Type admin_request_type);

  Http::CodecClient::Type downstreamProtocol() const { return downstream_protocol_; }
  // Prefix listener stat with IP:port, including IP version dependent loopback address.
  std::string listenerStatPrefix(const std::string& stat_name);

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
  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  // The codec type for the client-to-Envoy connection
  Http::CodecClient::Type downstream_protocol_{Http::CodecClient::Type::HTTP1};
  uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  std::string access_log_name_;
};
} // namespace Envoy
