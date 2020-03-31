#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class Http2UpstreamIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion,
                                               FakeHttpConnection::Type, Http::CodecClient::Type>>,
      public HttpIntegrationTest {
public:
  Http2UpstreamIntegrationTest()
      : HttpIntegrationTest(std::get<2>(GetParam()), std::get<0>(GetParam())) {}

  void SetUp() override {
    setDownstreamProtocol(std::get<2>(GetParam()));
    setUpstreamProtocol(std::get<1>(GetParam()));
  }

  void initialize() override { HttpIntegrationTest::initialize(); }

  void bidirectionalStreaming(uint32_t bytes);
  void simultaneousRequest(uint32_t request1_bytes, uint32_t request2_bytes,
                           uint32_t response1_bytes, uint32_t response2_bytes);
  void manySimultaneousRequests(uint32_t request_bytes, uint32_t response_bytes);
};
} // namespace Envoy
