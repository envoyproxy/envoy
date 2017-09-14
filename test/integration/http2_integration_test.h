#pragma once

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class Http2IntegrationTest : public HttpIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  Http2IntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override { setDownstreamProtocol(Http::CodecClient::Type::HTTP2); }

  void simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes);
};
} // namespace Envoy
