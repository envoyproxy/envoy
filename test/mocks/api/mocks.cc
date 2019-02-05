#include "mocks.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"

#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Api {

MockApi::MockApi() { ON_CALL(*this, fileSystem()).WillByDefault(ReturnRef(file_system_)); }

MockApi::~MockApi() {}

Event::DispatcherPtr MockApi::allocateDispatcher() {
  return Event::DispatcherPtr{allocateDispatcher_(timeSystem())};
}

MockOsSysCalls::MockOsSysCalls() {}

MockOsSysCalls::~MockOsSysCalls() {}

SysCallIntResult MockOsSysCalls::setsockopt(int sockfd, int level, int optname, const void* optval,
                                            socklen_t optlen) {
  ASSERT(optlen == sizeof(int));

  // Allow mocking system call failure.
  if (setsockopt_(sockfd, level, optname, optval, optlen) != 0) {
    return SysCallIntResult{-1, 0};
  }

  boolsockopts_[SockOptKey(sockfd, level, optname)] = !!*reinterpret_cast<const int*>(optval);
  return SysCallIntResult{0, 0};
};

SysCallIntResult MockOsSysCalls::getsockopt(int sockfd, int level, int optname, void* optval,
                                            socklen_t* optlen) {
  ASSERT(*optlen == sizeof(int));
  int val = 0;
  const auto& it = boolsockopts_.find(SockOptKey(sockfd, level, optname));
  if (it != boolsockopts_.end()) {
    val = it->second;
  }
  // Allow mocking system call failure.
  if (getsockopt_(sockfd, level, optname, optval, optlen) != 0) {
    return {-1, 0};
  }
  *reinterpret_cast<int*>(optval) = val;
  return {0, 0};
}

} // namespace Api
} // namespace Envoy
