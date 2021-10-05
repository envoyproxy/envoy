#pragma once

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class Http2UpstreamIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    upstream_tls_ = true;
    config_helper_.configureUpstreamTls(use_alpn_, upstreamProtocol() == Http::CodecType::HTTP3);
    HttpProtocolIntegrationTest::initialize();
  }

  void bidirectionalStreaming(uint32_t bytes);
  void manySimultaneousRequests(uint32_t request_bytes, uint32_t response_bytes);

  bool use_alpn_{false};

  uint64_t upstreamRxResetCounterValue();
  uint64_t upstreamTxResetCounterValue();
  uint64_t downstreamRxResetCounterValue();
  uint64_t downstreamTxResetCounterValue();
};
} // namespace Envoy
