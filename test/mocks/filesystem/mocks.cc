#include "test/mocks/filesystem/mocks.h"

#include "common/common/assert.h"
#include "common/common/lock_guard.h"

namespace Envoy {
namespace Filesystem {

MockFile::MockFile() : num_opens_(0), num_writes_(0), is_open_(false) {}
MockFile::~MockFile() = default;

Api::IoCallBoolResult MockFile::open(FlagSet flag) {
  Thread::LockGuard lock(open_mutex_);

  Api::IoCallBoolResult result = open_(flag);
  is_open_ = result.rc_;
  num_opens_++;
  open_event_.notifyOne();

  return result;
}

Api::IoCallSizeResult MockFile::write(absl::string_view buffer) {
  Thread::LockGuard lock(write_mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { NOT_REACHED_GCOVR_EXCL_LINE; })};
  }

  Api::IoCallSizeResult result = write_(buffer);
  num_writes_++;
  write_event_.notifyOne();

  return result;
}

Api::IoCallBoolResult MockFile::close() {
  Api::IoCallBoolResult result = close_();
  is_open_ = !result.rc_;

  return result;
}

MockInstance::MockInstance() = default;
MockInstance::~MockInstance() = default;

MockWatcher::MockWatcher() = default;
MockWatcher::~MockWatcher() = default;

} // namespace Filesystem
} // namespace Envoy
