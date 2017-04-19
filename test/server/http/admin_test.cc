#include "common/http/message_impl.h"
#include "common/profiler/profiler.h"

#include "server/http/admin.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;

namespace Server {

class AdminFilterTest : public testing::Test {
public:
  // TODO(mattklein123): Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", TestEnvironment::temporaryPath("envoy.prof"),
               Network::Utility::resolveUrl("tcp://127.0.0.1:9002"), server_),
        filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  AdminImpl admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
};

TEST_F(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeHeaders(request_headers_, true);
}

TEST_F(AdminFilterTest, Body) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeData(data, true);
}

TEST_F(AdminFilterTest, Trailers) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  filter_.decodeData(data, false);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeTrailers(request_headers_);
}

// Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
// a real profiler linked in (successful call to startProfiler). startProfiler
// requies tcmalloc.
#ifdef TCMALLOC

TEST_F(AdminFilterTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  admin_.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  admin_.runCallback("/cpuprofiler?enable=n", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

#endif

TEST_F(AdminFilterTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path("/dev/null",
                                   TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   Network::Utility::resolveUrl("tcp://127.0.0.1:9002"), server_);
  admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

} // namespace Server
