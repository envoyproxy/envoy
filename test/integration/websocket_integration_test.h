#pragma once

#include "envoy/http/codec.h"

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

struct WebsocketProtocolTestParams {
  Network::Address::IpVersion version;
  Http::CodecType downstream_protocol;
  Http::CodecType upstream_protocol;
};

class WebsocketIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override;

protected:
  void performUpgrade(const Http::TestRequestHeaderMapImpl& upgrade_request_headers,
                      const Http::TestResponseHeaderMapImpl& upgrade_response_headers,
                      bool upgrade_should_fail = false);
  void sendBidirectionalData();

  void validateUpgradeRequestHeaders(const Http::RequestHeaderMap& proxied_request_headers,
                                     const Http::RequestHeaderMap& original_request_headers);
  void validateUpgradeResponseHeaders(const Http::ResponseHeaderMap& proxied_response_headers,
                                      const Http::ResponseHeaderMap& original_response_headers);

  ABSL_MUST_USE_RESULT
  testing::AssertionResult waitForUpstreamDisconnectOrReset() {
    if (upstreamProtocol() != Http::CodecType::HTTP1) {
      return upstream_request_->waitForReset();
    } else {
      return fake_upstream_connection_->waitForDisconnect();
    }
  }

  void waitForClientDisconnectOrReset(
      Http::StreamResetReason reason = Http::StreamResetReason::RemoteReset) {
    if (downstreamProtocol() != Http::CodecType::HTTP1) {
      ASSERT_TRUE(response_->waitForReset());
      ASSERT_EQ(reason, response_->resetReason());
    } else {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    }
  }

  IntegrationStreamDecoderPtr response_;
};

} // namespace Envoy
