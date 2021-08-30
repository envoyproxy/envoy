#pragma once

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "test/common/http/http2/http2_frame.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/integration/h2_capture_fuzz.pb.h"
#include "test/integration/http_integration.h"

namespace Envoy {

class H2FuzzIntegrationTest : public HttpIntegrationTest {
public:
  H2FuzzIntegrationTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecType::HTTP2, version) {}

  void initialize() override;
  void replay(const test::integration::H2CaptureFuzzTestCase&, bool ignore_response);
  const std::chrono::milliseconds max_wait_ms_{10};

private:
  void sendFrame(const test::integration::H2TestFrame&,
                 std::function<void(const Envoy::Http::Http2::Http2Frame&)>);

  Filesystem::ScopedUseMemfiles use_memfiles_{true};
};
} // namespace Envoy
