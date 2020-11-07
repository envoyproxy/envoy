#include "mocks.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Api {

MockApi::MockApi() {
  ON_CALL(*this, fileSystem()).WillByDefault(ReturnRef(file_system_));
  ON_CALL(*this, rootScope()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, randomGenerator()).WillByDefault(ReturnRef(random_));
}

MockApi::~MockApi() = default;

Event::DispatcherPtr MockApi::allocateDispatcher(const std::string& name) {
  return Event::DispatcherPtr{allocateDispatcher_(name, time_system_)};
}
Event::DispatcherPtr MockApi::allocateDispatcher(const std::string& name,
                                                 Buffer::WatermarkFactoryPtr&& watermark_factory) {
  return Event::DispatcherPtr{
      allocateDispatcher_(name, std::move(watermark_factory), time_system_)};
}

MockOsSysCalls::MockOsSysCalls() {
  ON_CALL(*this, close(_)).WillByDefault(Invoke([](os_fd_t fd) {
#ifdef WIN32
    int rc = ::closesocket(fd);
    int last_error = ::GetLastError();
    // It might be the case that the fd is not actually a socket. In that case Winsock api is
    // failing with error `WSAENOTSOCK`. In that case we fall back to a regular close.
    if (last_error == WSAENOTSOCK) {
      rc = ::close(fd);
      last_error = ::GetLastError();
    }
    return SysCallIntResult{rc, last_error};
#else
    const int rc = ::close(fd);
    return SysCallIntResult{rc, errno};
#endif
  }));
}

MockOsSysCalls::~MockOsSysCalls() = default;

SysCallIntResult MockOsSysCalls::setsockopt(os_fd_t sockfd, int level, int optname,
                                            const void* optval, socklen_t optlen) {
  ASSERT(optlen == sizeof(int));

  // Allow mocking system call failure.
  if (setsockopt_(sockfd, level, optname, optval, optlen) != 0) {
    return SysCallIntResult{-1, 0};
  }

  boolsockopts_[SockOptKey(sockfd, level, optname)] = !!*reinterpret_cast<const int*>(optval);
  return SysCallIntResult{0, 0};
};

SysCallIntResult MockOsSysCalls::getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                                            socklen_t* optlen) {
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
