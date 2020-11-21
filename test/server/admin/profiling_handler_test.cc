#include "common/profiler/profiler.h"

#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, AdminCpuProfiler) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;

  // Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
  // a real profiler linked in (successful call to startProfiler).
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
#else
  EXPECT_EQ(Http::Code::InternalServerError,
            postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
#endif

  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, AdminHeapProfilerOnRepeatedRequest) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;
  auto repeatResultCode = Http::Code::BadRequest;
#ifndef PROFILER_AVAILABLE
  repeatResultCode = Http::Code::NotImplemented;
#endif

  postCallback("/heapprofiler?enable=y", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=y", header_map, data));

  postCallback("/heapprofiler?enable=n", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=n", header_map, data));
}

TEST_P(AdminInstanceTest, AdminHeapProfiler) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;

  // The below flow need to begin with the profiler not running
  Profiler::Heap::stopProfiler();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=n", header_map, data));
#else
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=n", header_map, data));
#endif

  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
}

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path(TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   server_);
  Http::TestResponseHeaderMapImpl header_map;
  const absl::string_view post = Http::Headers::get().MethodValues.Post;
  request_headers_.setMethod(post);
  admin_filter_.decodeHeaders(request_headers_, false);
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::InternalServerError,
                           admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map,
                                                              data, admin_filter_)));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

} // namespace Server
} // namespace Envoy
