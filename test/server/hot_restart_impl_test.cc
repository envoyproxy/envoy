#include <memory>

#include "common/api/os_sys_calls_impl.h"
#include "common/api/os_sys_calls_impl_hot_restart.h"
#include "common/common/hex.h"

#include "server/hot_restart_impl.h"

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
using testing::Return;
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
    // We bind two sockets: one to talk to parent, one to talk to our (hypothetical eventual) child
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(2);

    // Test we match the correct stat with empty-slots before, after, or both.
    hot_restart_ = std::make_unique<HotRestartImpl>(0, 0);
    hot_restart_->drainParentListeners();

    // We close both sockets.
    EXPECT_CALL(os_sys_calls_, close(_)).Times(2);
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

// Test that HotRestartDomainSocketInUseException is thrown when the domain socket is already
// in use,
TEST_F(HotRestartImplTest, DomainSocketAlreadyInUse) {
  EXPECT_CALL(os_sys_calls_, bind(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_ADDR_IN_USE}));
  EXPECT_CALL(os_sys_calls_, close(_)).Times(1);

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0),
               Server::HotRestartDomainSocketInUseException);
}

// Test that EnvoyException is thrown when the domain socket bind fails for reasons other than
// being in use.
TEST_F(HotRestartImplTest, DomainSocketError) {
  EXPECT_CALL(os_sys_calls_, bind(_, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_ACCESS}));
  EXPECT_CALL(os_sys_calls_, close(_)).Times(1);

  EXPECT_THROW(std::make_unique<HotRestartImpl>(0, 0), EnvoyException);
}

} // namespace
} // namespace Server
} // namespace Envoy
