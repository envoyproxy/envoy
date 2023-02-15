#include "test/fuzz/fuzz_runner.h"
#include "test/server/config_validation/xds_fuzz.h"
#include "test/server/config_validation/xds_fuzz.pb.validate.h"

namespace Envoy {
DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }
  XdsFuzzTest test(input);
  test.replay();
  XdsFuzzTest test_with_unified_mux(input, true);
  test_with_unified_mux.replay();
#else
  UNREFERENCED_PARAMETER(input);
#endif
}
} // namespace Envoy
