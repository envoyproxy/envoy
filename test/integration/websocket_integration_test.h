#pragma once

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct WebsocketProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecClient::Type downstream_protocol;
  FakeHttpConnection::Type upstream_protocol;
};

class WebsocketIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override;

protected:
  void performUpgrade(const Http::TestHeaderMapImpl& upgrade_request_headers,
                      const Http::TestHeaderMapImpl& upgrade_response_headers);
  void sendBidirectionalData();

  void validateUpgradeRequestHeaders(const Http::HeaderMap& proxied_request_headers,
                                     const Http::HeaderMap& original_request_headers);
  void validateUpgradeResponseHeaders(const Http::HeaderMap& proxied_response_headers,
                                      const Http::HeaderMap& original_response_headers);
  void commonValidate(Http::HeaderMap& proxied_headers, const Http::HeaderMap& original_headers);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult waitForUpstreamDisconnectOrReset() {
    if (upstreamProtocol() != FakeHttpConnection::Type::HTTP1) {
      return upstream_request_->waitForReset();
    } else {
      return fake_upstream_connection_->waitForDisconnect();
    }
  }

  void waitForClientDisconnectOrReset() {
    if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
      response_->waitForReset();
    } else {
      codec_client_->waitForDisconnect();
    }
  }

  IntegrationStreamDecoderPtr response_;
};

} // namespace Envoy
