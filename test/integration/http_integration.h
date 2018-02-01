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

  void makeHeaderOnlyRequest(const Http::HeaderMap& headers, IntegrationStreamDecoder& response);
  void makeRequestWithBody(const Http::HeaderMap& headers, uint64_t body_size,
                           IntegrationStreamDecoder& response);
  bool sawGoAway() { return saw_goaway_; }
  void sendData(Http::StreamEncoder& encoder, Buffer::Instance& data, bool end_stream);
  void sendData(Http::StreamEncoder& encoder, uint64_t size, bool end_stream);
  void sendTrailers(Http::StreamEncoder& encoder, const Http::HeaderMap& trailers);
  void sendReset(Http::StreamEncoder& encoder);
  Http::StreamEncoder& startRequest(const Http::HeaderMap& headers,
                                    IntegrationStreamDecoder& response);
  void waitForDisconnect();

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

  ConnectionCallbacks callbacks_;
  CodecCallbacks codec_callbacks_;
  bool connected_{};
  bool disconnected_{};
  bool saw_goaway_{};
};

typedef std::unique_ptr<IntegrationCodecClient> IntegrationCodecClientPtr;

/**
 * Test fixture for HTTP and HTTP/2 integration tests.
 */
class HttpIntegrationTest : public BaseIntegrationTest {
public:
  HttpIntegrationTest(Http::CodecClient::Type downstream_protocol,
                      Network::Address::IpVersion version,
                      const std::string& config = ConfigHelper::HTTP_PROXY_CONFIG);
  virtual ~HttpIntegrationTest();

protected:
  IntegrationCodecClientPtr makeHttpConnection(uint32_t port);
  IntegrationCodecClientPtr makeHttpConnection(Network::ClientConnectionPtr&& conn);

  // Sets downstream_protocol_ and alters the HTTP connection manager codec type in the
  // config_helper_.
  void setDownstreamProtocol(Http::CodecClient::Type type);

  // Sends |request_headers| and |request_body_size| bytes of body upstream.
  // Configured upstream to send |response_headers| and |response_body_size|
  // bytes of body downstream.
  //
  // Waits for the complete downstream response before returning.
  // Requires |codec_client_| to be initialized.
  void sendRequestAndWaitForResponse(Http::TestHeaderMapImpl& request_headers,
                                     uint32_t request_body_size,
                                     Http::TestHeaderMapImpl& response_headers,
                                     uint32_t response_body_size);

  // Wait for the end of stream on the next upstream stream on fake_upstreams_
  // Sets fake_upstream_connection_ to the connection and upstream_request_ to stream.
  void waitForNextUpstreamRequest(uint64_t upstream_index = 0);

  // Close |codec_client_| and |fake_upstream_connection_| cleanly.
  void cleanupUpstreamAndDownstream();

  typedef std::function<Network::ClientConnectionPtr()> ConnectionCreationFunction;

  void testRouterRedirect();
  void testRouterDirectResponse();
  void testRouterNotFound();
  void testRouterNotFoundWithBody();
  void testRouterClusterNotFound404();
  void testRouterClusterNotFound503();
  void testRouterRequestAndResponseWithBody(uint64_t request_size, uint64_t response_size,
                                            bool big_header,
                                            ConnectionCreationFunction* creator = nullptr);
  void testRouterHeaderOnlyRequestAndResponse(bool close_upstream,
                                              ConnectionCreationFunction* creator = nullptr);
  void testRouterUpstreamDisconnectBeforeRequestComplete();
  void
  testRouterUpstreamDisconnectBeforeResponseComplete(ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeRequestComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterDownstreamDisconnectBeforeResponseComplete(
      ConnectionCreationFunction* creator = nullptr);
  void testRouterUpstreamResponseBeforeRequestComplete();
  void testTwoRequests();
  void testOverlyLongHeaders();
  // HTTP/1 tests
  void testBadFirstline();
  void testMissingDelimiter();
  void testInvalidCharacterInFirstline();
  void testLowVersion();
  void testHttp10Request();
  void testNoHost();
  void testUpstreamProtocolError();
  void testBadPath();
  void testAbsolutePath();
  void testAbsolutePathWithPort();
  void testAbsolutePathWithoutPort();
  void testConnect();
  void testAllowAbsoluteSameRelative();
  // Test that a request returns the same content with both allow_absolute_urls enabled and
  // allow_absolute_urls disabled
  void testEquivalent(const std::string& request);
  void testValidZeroLengthContent();
  void testInvalidContentLength();
  void testMultipleContentLengths();
  void testDrainClose();
  void testRetry();
  void testRetryHittingBufferLimit();
  void testGrpcRetry();
  void testHittingDecoderFilterLimit();
  void testHittingEncoderFilterLimit();

  // HTTP/2 client tests.
  void testDownstreamResetBeforeResponseComplete();
  void testTrailers(uint64_t request_size, uint64_t response_size);

  Http::CodecClient::Type downstreamProtocol() const { return downstream_protocol_; }

  // The client making requests to Envoy.
  IntegrationCodecClientPtr codec_client_;
  // A placeholder for the first upstream connection.
  FakeHttpConnectionPtr fake_upstream_connection_;
  // A placeholder for the first response received by the client.
  IntegrationStreamDecoderPtr response_{new IntegrationStreamDecoder(*dispatcher_)};
  // A placeholder for the first request received at upstream.
  FakeStreamPtr upstream_request_;
  // A pointer to the request encoder, if used.
  Http::StreamEncoder* request_encoder_{nullptr};
  // The response headers sent by sendRequestAndWaitForResponse() by default.
  Http::TestHeaderMapImpl default_response_headers_{{":status", "200"}};
  // The codec type for the client-to-Envoy connection
  Http::CodecClient::Type downstream_protocol_{Http::CodecClient::Type::HTTP1};
};
} // namespace Envoy
