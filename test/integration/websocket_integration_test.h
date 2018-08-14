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
  void TearDown() override {
    fake_tcp_upstream_connection_.reset();
    tcp_client_.reset();
  }

protected:
  void performWebSocketUpgrade(const std::string& upgrade_req_string,
                               const std::string& upgrade_resp_string);
  void sendBidirectionalData();

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

  static std::string createUpgradeRequest(absl::string_view upgrade_type,
                                          absl::optional<uint32_t> content_length = absl::nullopt) {
    std::string content_length_string =
        content_length.has_value() ? fmt::format("Content-Length: {}\r\n", content_length.value())
                                   : "";
    return fmt::format("GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                       "keep-alive, Upgrade\r\nUpgrade: {}\r\n{}\r\n",
                       upgrade_type, content_length_string);
  }

  static std::string createUpgradeResponse(absl::string_view upgrade_type) {
    return fmt::format(
        "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: {}\r\n\r\n",
        upgrade_type);
  }

  const std::string upgrade_req_str_ = createUpgradeRequest("websocket");
  const std::string upgrade_resp_str_ = createUpgradeResponse("websocket");

  const std::string modified_upgrade_resp_str_ = "HTTP/1.1 101 Switching Protocols\r\nconnection: "
                                                 "Upgrade\r\nupgrade: websocket\r\ncontent-length: "
                                                 "0\r\n\r\n";

  FakeRawConnectionPtr fake_tcp_upstream_connection_;
  IntegrationTcpClientPtr tcp_client_;
};

} // namespace Envoy
