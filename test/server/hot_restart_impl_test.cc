#include <memory>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/api/os_sys_calls_impl_hot_restart.h"
#include "source/common/common/hex.h"
#include "source/server/hot_restart_impl.h"

#include "test/mocks/api/hot_restart.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/server/hot_restart.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::WithArg;

namespace Envoy {
namespace Server {
namespace {

class HotRestartImplTest : public testing::Test {
public:
  void setup() {
    EXPECT_CALL(hot_restart_os_sys_calls_, shmUnlink(_)).Times(AnyNumber());
    EXPECT_CALL(hot_restart_os_sys_calls_, shmOpen(_, _, _));
    EXPECT_CALL(os_sys_calls_, ftruncate(_, _)).WillOnce(WithArg<1>(Invoke([this](off_t size) {
      buffer_.resize(size);
      return Api::SysCallIntResult{0, 0};
    })));
    EXPECT_CALL(os_sys_calls_, mmap(_, _, _, _, _, _)).WillOnce(InvokeWithoutArgs([this]() {
      return Api::SysCallPtrResult{buffer_.data(), 0};
    }));
    // We bind two sockets, from both ends (parent and child), totaling four sockets to be bound.
    // The socket for child->parent RPCs, and the socket for parent->child UDP forwarding
    // in support of QUIC during hot restart.
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(4);

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_ = std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0);
    hot_restart_->drainParentListeners();

    // We close both sockets, both ends, totaling 4.
    EXPECT_CALL(os_sys_calls_, close(_)).Times(4);
  }

  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
  Api::MockHotRestartOsSysCalls hot_restart_os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::HotRestartOsSysCallsImpl> hot_restart_os_calls{
      &hot_restart_os_sys_calls_};
  std::vector<uint8_t> buffer_;
  std::unique_ptr<HotRestartImpl> hot_restart_;
};

TEST_F(HotRestartImplTest, VersionString) {
  // Tests that the version-string will be consistent and HOT_RESTART_VERSION,
  // between multiple instantiations.
  std::string version;

  // The mocking infrastructure requires a test setup and teardown every time we
  // want to re-instantiate HotRestartImpl.
  {
    setup();
    version = hot_restart_->version();
    EXPECT_TRUE(absl::StartsWith(version, fmt::format("{}.", HOT_RESTART_VERSION))) << version;
    TearDown();
  }

  {
    setup();
    EXPECT_EQ(version, hot_restart_->version()) << "Version string deterministic from options";
    TearDown();
  }
}

class DomainSocketErrorTest : public HotRestartImplTest, public testing::WithParamInterface<int> {};

// The parameter is the number of sockets that bind including the one that errors.
INSTANTIATE_TEST_CASE_P(SocketIndex, DomainSocketErrorTest, ::testing::Values(1, 2, 3, 4));

// Test that HotRestartDomainSocketInUseException is thrown when any of the domain sockets is
// already in use.
TEST_P(DomainSocketErrorTest, DomainSocketAlreadyInUse) {
  int i = 0;
  EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(GetParam()).WillRepeatedly([&i]() {
    if (++i == GetParam()) {
      return Api::SysCallIntResult{-1, SOCKET_ERROR_ADDR_IN_USE};
    }
    return Api::SysCallIntResult{0, 0};
  });
  EXPECT_CALL(os_sys_calls_, close(_)).Times(GetParam());

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0),
               Server::HotRestartDomainSocketInUseException);
}

// Test that EnvoyException is thrown when any of the the domain socket bind fails
// for reasons other than being in use.
TEST_P(DomainSocketErrorTest, DomainSocketError) {
  int i = 0;
  EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(GetParam()).WillRepeatedly([&i]() {
    if (++i == GetParam()) {
      return Api::SysCallIntResult{-1, SOCKET_ERROR_ACCESS};
    }
    return Api::SysCallIntResult{0, 0};
  });
  EXPECT_CALL(os_sys_calls_, close(_)).Times(GetParam());

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0, "@envoy_domain_socket", 0), EnvoyException);
}

} // namespace
} // namespace Server
} // namespace Envoy
