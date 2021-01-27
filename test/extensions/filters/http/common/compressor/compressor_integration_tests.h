#pragma once

#include "envoy/http/codec.h"

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class WebsocketWithCompressorIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override;

protected:
  void performUpgrade(const Http::TestRequestHeaderMapImpl& upgrade_request_headers,
                      const Http::TestResponseHeaderMapImpl& upgrade_response_headers);
  void sendBidirectionalData();

  void validateUpgradeRequestHeaders(const Http::RequestHeaderMap& proxied_request_headers,
                                     const Http::RequestHeaderMap& original_request_headers);
  void validateUpgradeResponseHeaders(const Http::ResponseHeaderMap& proxied_response_headers,
                                      const Http::ResponseHeaderMap& original_response_headers);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult waitForUpstreamDisconnectOrReset() {
    if (upstreamProtocol() != FakeHttpConnection::Type::HTTP1) {
      return upstream_request_->waitForReset();
    } else {
      return fake_upstream_connection_->waitForDisconnect();
    }
  }

  IntegrationStreamDecoderPtr response_;
};

class CompressorProxyingConnectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override;
  Http::TestRequestHeaderMapImpl connect_headers_{{":method", "CONNECT"},
                                                  {":path", "/"},
                                                  {":protocol", "bytestream"},
                                                  {":scheme", "https"},
                                                  {":authority", "host:80"}};
  IntegrationStreamDecoderPtr response_;
};

} // namespace Envoy
