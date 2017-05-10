#include <fstream>

#include "common/http/message_impl.h"
#include "common/profiler/profiler.h"

#include "server/http/admin.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::NiceMock;

namespace Server {

class AdminFilterTest : public testing::Test {
public:
  // TODO(mattklein123): Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", TestEnvironment::temporaryPath("envoy.prof"),
               TestEnvironment::temporaryPath("admin.address"),
               Network::Utility::resolveUrl("tcp://127.0.0.1:0"), server_),
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

class AdminInstanceTest : public testing::Test {
public:
  AdminInstanceTest()
      : address_out_path_(TestEnvironment::temporaryPath("admin.address")),
        cpu_profile_path_(TestEnvironment::temporaryPath("envoy.prof")),
        admin_("/dev/null", cpu_profile_path_, address_out_path_,
               Network::Test::getSomeLoopbackAddress(Network::Address::IpVersion::v4), server_) {}

  std::string address_out_path_;
  std::string cpu_profile_path_;
  NiceMock<MockInstance> server_;
  AdminImpl admin_;
};

// Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
// a real profiler linked in (successful call to startProfiler). startProfiler
// requies tcmalloc.
#ifdef TCMALLOC

TEST_F(AdminInstanceTest, AdminProfiler) {
  Buffer::OwnedImpl data;
  admin_.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
  admin_.runCallback("/cpuprofiler?enable=n", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

#endif

TEST_F(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path(
      "/dev/null", TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"), "",
      Network::Test::getSomeLoopbackAddress(Network::Address::IpVersion::v4), server_);
  admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", data);
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_F(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_F(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(
      "/dev/null", cpu_profile_path_, bad_path,
      Network::Test::getSomeLoopbackAddress(Network::Address::IpVersion::v4), server_);
  EXPECT_FALSE(std::ifstream(bad_path));
}
} // namespace Server
} // Envoy
