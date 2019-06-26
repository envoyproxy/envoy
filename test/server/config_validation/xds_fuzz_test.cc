#include "test/server/config_validation/xds_fuzz.h"
#include "test/server/config_validation/xds_fuzz.pb.h"

namespace Envoy {
namespace Server {

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  XdsFuzzTest test(ip_version, input);
  test.replay();
}

} // namespace Server
} // namespace Envoy
