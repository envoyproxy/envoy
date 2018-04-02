#include "mocks.h"

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::_;

namespace Envoy {
namespace Api {

MockApi::MockApi() { ON_CALL(*this, createFile(_, _, _, _)).WillByDefault(Return(file_)); }

MockApi::~MockApi() {}

MockOsSysCalls::MockOsSysCalls() { num_writes_ = num_open_ = 0; }

MockOsSysCalls::~MockOsSysCalls() {}

int MockOsSysCalls::open(const std::string& full_path, int flags, int mode) {
  std::unique_lock<Thread::BasicLockable> lock(open_mutex_);

  int result = open_(full_path, flags, mode);
  num_open_++;
  open_event_.notify_one();

  return result;
}

ssize_t MockOsSysCalls::write(int fd, const void* buffer, size_t num_bytes) {
  std::unique_lock<Thread::BasicLockable> lock(write_mutex_);

  ssize_t result = write_(fd, buffer, num_bytes);
  num_writes_++;
  write_event_.notify_one();

  return result;
}

int MockOsSysCalls::setsockopt(int sockfd, int level, int optname, const void* optval,
                               socklen_t optlen) {
  ASSERT(optlen == sizeof(int));

  // Allow mocking system call failure.
  if (setsockopt_(sockfd, level, optname, optval, optlen) != 0) {
    return -1;
  }

  boolsockopts_[SockOptKey(sockfd, level, optname)] = !!*reinterpret_cast<const int*>(optval);
  return 0;
};

int MockOsSysCalls::getsockopt(int sockfd, int level, int optname, void* optval,
                               socklen_t* optlen) {
  ASSERT(*optlen == sizeof(int));
  int val = 0;
  const auto& it = boolsockopts_.find(SockOptKey(sockfd, level, optname));
  if (it != boolsockopts_.end()) {
    val = it->second;
  }
  // Allow mocking system call failure.
  if (getsockopt_(sockfd, level, optname, optval, optlen) != 0) {
    return -1;
  }
  *reinterpret_cast<int*>(optval) = val;
  return 0;
}

} // namespace Api
} // namespace Envoy
