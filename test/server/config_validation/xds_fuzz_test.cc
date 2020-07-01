/* #include "common/protobuf/utility.h" */

#include "test/fuzz/fuzz_runner.h"
#include "test/server/config_validation/xds_fuzz.h"
#include "test/server/config_validation/xds_fuzz.pb.validate.h"

namespace Envoy {

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }
  XdsFuzzTest test(input, envoy::config::core::v3::ApiVersion::V2);
  test.replay();
}

} // namespace Envoy
