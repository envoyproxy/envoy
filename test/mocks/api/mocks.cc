#include "mocks.h"

#include "envoy/thread/thread.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnNew;

namespace Filesystem {

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

} // Filesystem

namespace Api {

MockApi::MockApi() {
  ON_CALL(*this, createFile_(_, _, _, _))
      .WillByDefault(ReturnNew<NiceMock<Filesystem::MockFile>>());
}

MockApi::~MockApi() {}

} // Api
