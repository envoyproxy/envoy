#include "mocks.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Api {

MockApi::MockApi() { ON_CALL(*this, createFile(_, _, _)).WillByDefault(Return(file_)); }

MockApi::~MockApi() {}

MockOsSysCalls::MockOsSysCalls() { num_writes_ = num_open_ = 0; }

MockOsSysCalls::~MockOsSysCalls() {}

SysCallIntResult MockOsSysCalls::open(const std::string& full_path, int flags, int mode) {
  Thread::LockGuard lock(open_mutex_);

  int rc = open_(full_path, flags, mode);
  num_open_++;
  open_event_.notifyOne();

  return SysCallIntResult{rc, errno};
}

SysCallSizeResult MockOsSysCalls::write(int fd, const void* buffer, size_t num_bytes) {
  Thread::LockGuard lock(write_mutex_);

  ssize_t rc = write_(fd, buffer, num_bytes);
  num_writes_++;
  write_event_.notifyOne();

  return SysCallSizeResult{rc, errno};
}

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
