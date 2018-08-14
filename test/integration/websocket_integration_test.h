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
  bool old_style_websockets_{std::get<1>(GetParam())};

protected:
  void performUpgrade(const Http::TestHeaderMapImpl& upgrade_request_headers,
                      const Http::TestHeaderMapImpl& upgrade_response_headers);
  void sendBidirectionalData();

  void validateInitialUpstreamData(const std::string& received_data, bool initial_headers_chunked);
  void validateInitialDownstreamData(const std::string& received_data,
                                     const std::string& expected_data);
  void validateFinalDownstreamData(const std::string& received_data,
                                   const std::string& expected_data);
  void validateFinalUpstreamData(const std::string& received_data,
                                 const std::string& expected_data);

  IntegrationStreamDecoderPtr response_;
};

} // namespace Envoy
