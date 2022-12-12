#include "test/mocks/filesystem/mocks.h"

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"

namespace Envoy {
namespace Filesystem {

MockFile::MockFile() : num_opens_(0), num_writes_(0), is_open_(false) {}
MockFile::~MockFile() = default;

Api::IoCallBoolResult MockFile::open(FlagSet flag) {
  Thread::LockGuard lock(open_mutex_);

  Api::IoCallBoolResult result = open_(flag);
  is_open_ = result.return_value_;
  num_opens_++;
  open_event_.notifyOne();

  return result;
}

Api::IoCallSizeResult MockFile::write(absl::string_view buffer) {
  Thread::LockGuard lock(write_mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = write_(buffer);
  num_writes_++;
  write_event_.notifyOne();

  return result;
}

Api::IoCallSizeResult MockFile::pread(void* buf, size_t count, off_t offset) {
  Thread::LockGuard lock(pread_mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = pread_(buf, count, offset);
  num_preads_++;
  pread_event_.notifyOne();

  return result;
}

Api::IoCallSizeResult MockFile::pwrite(const void* buf, size_t count, off_t offset) {
  Thread::LockGuard lock(pwrite_mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = pwrite_(buf, count, offset);
  num_pwrites_++;
  pwrite_event_.notifyOne();

  return result;
}

Api::IoCallBoolResult MockFile::close() {
  Api::IoCallBoolResult result = close_();
  is_open_ = !result.return_value_;

  return result;
}

MockInstance::MockInstance() = default;
MockInstance::~MockInstance() = default;

MockWatcher::MockWatcher() = default;
MockWatcher::~MockWatcher() = default;

} // namespace Filesystem
} // namespace Envoy
