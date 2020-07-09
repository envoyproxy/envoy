#include "test/integration/h2_fuzz.h"

namespace Envoy {
void H2FuzzIntegrationTest::initialize() {
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

  HttpIntegrationTest::initialize();
}

DEFINE_PROTO_FUZZER(const test::integration::H2CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  FUZZ_ASSERT(!TestEnvironment::getIpVersionsForTest().empty());
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  PERSISTENT_FUZZ_VAR H2FuzzIntegrationTest h2_fuzz_integration_test(ip_version);
  h2_fuzz_integration_test.replay(input, false);
}

} // namespace Envoy
