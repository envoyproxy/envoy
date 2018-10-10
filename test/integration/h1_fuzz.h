#pragma once

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/capture_fuzz.pb.h"
#include "test/integration/http_integration.h"

namespace Envoy {

class H1FuzzIntegrationTest : public HttpIntegrationTest {
public:
  H1FuzzIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, version, realTime()) {}

  void initialize() override;
  void replay(const test::integration::CaptureFuzzTestCase&);
  const std::chrono::milliseconds max_wait_ms_{10};
};
} // namespace Envoy
