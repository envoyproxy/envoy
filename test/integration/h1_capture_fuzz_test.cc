#include <functional>

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/capture_fuzz.pb.h"
#include "test/integration/h1_fuzz.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {

void H1FuzzIntegrationTest::initialize() { HttpIntegrationTest::initialize(); }

DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  RELEASE_ASSERT(TestEnvironment::getIpVersionsForTest().size() > 0, "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  H1FuzzIntegrationTest h1_fuzz_integration_test(ip_version);
  h1_fuzz_integration_test.replay(input);
}

} // namespace Envoy
