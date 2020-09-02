#include "test/common/upstream/health_check_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"

//#include "test/server/config_validation/xds_fuzz.h"
//#include "test/server/config_validation/xds_fuzz.pb.validate.h"
#include "test/common/upstream/health_check_fuzz.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::HealthCheckTestCase input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }
  
  HealthCheckFuzz health_check_fuzz;
  health_check_fuzz.initialize(input);
}

} // namespace Upstream
} // namespace Envoy
