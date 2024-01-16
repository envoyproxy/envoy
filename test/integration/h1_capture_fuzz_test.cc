#include "test/integration/h1_fuzz.h"

namespace Envoy {
void H1FuzzIntegrationTest::initialize() { HttpIntegrationTest::initialize(); }

DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  PERSISTENT_FUZZ_VAR(H1FuzzIntegrationTest, h1_fuzz_integration_test, (ip_version));
  h1_fuzz_integration_test.replay(input, false);
}

} // namespace Envoy
