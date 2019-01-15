#include "test/mocks/filesystem/mocks.h"

#include <string>

#include "common/common/lock_guard.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Filesystem {

MockFile::MockFile() {
  num_writes_ = num_open_ = 0;
  is_open_ = true;
}
MockFile::~MockFile() {}

class OpenCleanup {
public:
  OpenCleanup(std::function<void()> cleanup) : cleanup_(cleanup) {}
  ~OpenCleanup() { cleanup_(); }

  std::function<void()> cleanup_;
};

void MockFile::open() {
  Thread::LockGuard lock(open_mutex_);
  bool open_succeeded = false;
  OpenCleanup oc([&]() {
    num_open_++;
    is_open_ = open_succeeded;
    open_event_.notifyOne();
  });

  open_();
  open_succeeded = true;

  return;
}

Api::SysCallSizeResult MockFile::write(const void* buffer, size_t num_bytes) {
  Thread::LockGuard lock(write_mutex_);

  if (!is_open_) {
    return {-1, EBADF};
  }

  Api::SysCallSizeResult rc = write_(buffer, num_bytes);
  num_writes_++;
  write_event_.notifyOne();

  return rc;
}

void MockFile::close() {
  close_();
  is_open_ = false;
}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

MockStatsFile::MockStatsFile() {}
MockStatsFile::~MockStatsFile() {}

MockStatsInstance::MockStatsInstance() {}

MockStatsInstance::~MockStatsInstance() {}

MockWatcher::MockWatcher() {}
MockWatcher::~MockWatcher() {}

} // namespace Filesystem
} // namespace Envoy
