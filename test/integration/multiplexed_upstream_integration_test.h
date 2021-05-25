#pragma once

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class Http2UpstreamIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(use_alpn_,
                                        upstreamProtocol() == FakeHttpConnection::Type::HTTP3);
    HttpProtocolIntegrationTest::initialize();
  }

  void bidirectionalStreaming(uint32_t bytes);
  void simultaneousRequest(uint32_t request1_bytes, uint32_t request2_bytes,
                           uint32_t response1_bytes, uint32_t response2_bytes);
  void manySimultaneousRequests(uint32_t request_bytes, uint32_t response_bytes);

  bool use_alpn_{false};
};
} // namespace Envoy
