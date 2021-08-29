#include "test/server/admin/admin_instance.h"

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, AdminPprofHeap) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;

#ifdef TCMALLOC
  EXPECT_EQ(Http::Code::OK, postCallback("/pprof/heap", header_map, data));
  EXPECT_EQ("application/octet-stream", header_map.contentType());
#else
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/pprof/heap", header_map, data));
#endif
}

} // namespace Server
} // namespace Envoy
