#include "common/http/message_impl.h"
#include "common/profiler/profiler.h"
#include "server/http/admin.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;

namespace Server {

class AdminFilterTest : public testing::Test {
public:
  // TODO(mattklein123): Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", "/tmp/envoy.prof", Network::Utility::resolveUrl("tcp://127.0.0.1:9002"),
               server_),
        admin_bad_profiler_path_("/dev/null", "/var/log/envoy/envoy.prof",
                                 Network::Utility::resolveUrl("tcp://127.0.0.1:9002"), server_),
        filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  AdminImpl admin_;
  AdminImpl admin_bad_profiler_path_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::TestHeaderMapImpl request_headers_;
  Http::TestHeaderMapImpl profiler_headers_;
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

TEST_F(AdminFilterTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  admin_.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  admin_.runCallback("/cpuprofiler?enable=n", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_F(AdminFilterTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  admin_bad_profiler_path_.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

} // namespace Server
