#include "test/server/config_validation/xds_fuzz.h"

namespace Envoy {

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  XdsFuzzTest test(input);
  test.replay();
}

} // namespace Envoy
