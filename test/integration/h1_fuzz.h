#pragma once

#include "test/integration/http_integration.h"

namespace Envoy {

class H1FuzzIntegrationTest : public HttpIntegrationTest {
public:
  H1FuzzIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, version) {}

  void initialize() override;
  void replay(const test::integration::CaptureFuzzTestCase&);
  const std::chrono::milliseconds max_wait_ms_{10};
};
} // namespace Envoy
