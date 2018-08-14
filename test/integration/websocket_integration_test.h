#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class WebsocketIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
public:
  void initialize() override;
  WebsocketIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, std::get<0>(GetParam())) {}

protected:
  void performUpgrade(const Http::TestHeaderMapImpl& upgrade_request_headers,
                      const Http::TestHeaderMapImpl& upgrade_response_headers);
  void sendBidirectionalData();

  void validateUpgradeRequestHeaders(const Http::HeaderMap& proxied_request_headers,
                                     const Http::HeaderMap& original_request_headers);

  void validateUpgradeResponseHeaders(const Http::HeaderMap& proxied_response_headers,
                                      const Http::HeaderMap& original_response_headers);

  void commonValidate(Http::HeaderMap& proxied_headers, const Http::HeaderMap& original_headers);

  // True if the test uses "old style" TCP proxy websockets. False to use the
  // new style "HTTP filter chain" websockets.
  // See
  // https://github.com/envoyproxy/envoy/blob/master/docs/root/intro/arch_overview/websocket.rst
  bool old_style_websockets_{std::get<1>(GetParam())};
  IntegrationStreamDecoderPtr response_;
};

} // namespace Envoy
