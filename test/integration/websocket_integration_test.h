#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class WebsocketIntegrationTest : public HttpIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  void initialize() override;
  WebsocketIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

protected:
  void validateInitialUpstreamData(const std::string& received_data);
  void validateInitialDownstreamData(const std::string& received_data);
  void validateFinalDownstreamData(const std::string& received_data,
                                   const std::string& expected_data);
  void validateFinalUpstreamData(const std::string& received_data,
                                 const std::string& expected_data);

  const std::string upgrade_req_str_ = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                       "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str_ =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
};

} // namespace Envoy
