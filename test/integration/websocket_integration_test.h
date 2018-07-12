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
  void validateInitialUpstreamData(const std::string& received_data);
  void validateInitialDownstreamData(const std::string& received_data,
                                     const std::string& expected_data);
  void validateFinalDownstreamData(const std::string& received_data,
                                   const std::string& expected_data);
  void validateFinalUpstreamData(const std::string& received_data,
                                 const std::string& expected_data);

  const std::string& downstreamRespStr() {
    return old_style_websockets_ ? upgrade_resp_str_ : modified_upgrade_resp_str_;
  }

  const std::string upgrade_req_str_ = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                       "keep-alive, Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str_ =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";

  const std::string modified_upgrade_resp_str_ = "HTTP/1.1 101 Switching Protocols\r\nconnection: "
                                                 "Upgrade\r\nupgrade: websocket\r\ncontent-length: "
                                                 "0\r\n\r\n";
};

} // namespace Envoy
