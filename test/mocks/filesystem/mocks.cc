#include "test/mocks/filesystem/mocks.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"

namespace Envoy {
namespace Filesystem {

MockFile::MockFile() : File(""), num_opens_(0), num_writes_(0) {}
MockFile::~MockFile() {}

void MockFile::openFile() {
  Thread::LockGuard lock(open_mutex_);

  const bool result = open_();
  setFileOpen(result);
  num_opens_++;
  open_event_.notifyOne();
}

ssize_t MockFile::writeFile(absl::string_view buffer) {
  Thread::LockGuard lock(write_mutex_);
  if (!isOpen()) {
    errno = EBADF;
    return -1;
  }

  const ssize_t result = write_(buffer);
  num_writes_++;
  write_event_.notifyOne();

  return result;
}

bool MockFile::closeFile() { return close_(); }

void MockFile::setFileOpen(bool is_open) { fd_ = is_open ? 1 : -1; }

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

MockWatcher::MockWatcher() {}
MockWatcher::~MockWatcher() {}

} // namespace Filesystem
} // namespace Envoy
