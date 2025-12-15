#include "test/server/admin/admin_instance.h"

#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::HasSubstr;

namespace Envoy {
namespace Server {

#if defined(__linux__)
TEST_P(AdminInstanceTest, CpuWorkers) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  EXPECT_EQ(Http::Code::OK, getCallback("/cpu/workers", header_map, response));
  EXPECT_THAT(response.toString(),
              HasSubstr("Each worker thread CPU utilization (similar to Linux top):"));
}
#endif

} // namespace Server
} // namespace Envoy


