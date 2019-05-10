#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class HttpTimeoutIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public Event::TestUsingSimulatedTime,
                                   public HttpIntegrationTest {
public:
  // Arbitrarily choose HTTP2 here, the tests for this class are around
  // timeouts which don't have version specific behavior.
  HttpTimeoutIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override {
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  }

  void testRouterRequestAndResponseWithHedgedPerTryTimeout(uint64_t request_size,
                                                           uint64_t response_size,
                                                           bool first_request_wins);
};

} // namespace Envoy
