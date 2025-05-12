#include "test/mocks/filesystem/mocks.h"

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"

namespace Envoy {
namespace Filesystem {

MockFile::MockFile() = default;
MockFile::~MockFile() = default;

Api::IoCallBoolResult MockFile::open(FlagSet flag) {
  absl::MutexLock lock(&mutex_);

  Api::IoCallBoolResult result = open_(flag);
  is_open_ = result.return_value_;
  num_opens_++;

  return result;
}

Api::IoCallSizeResult MockFile::write(absl::string_view buffer) {
  absl::MutexLock lock(&mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = write_(buffer);
  num_writes_++;

  return result;
}

Api::IoCallSizeResult MockFile::pread(void* buf, uint64_t count, uint64_t offset) {
  absl::MutexLock lock(&mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = pread_(buf, count, offset);
  num_preads_++;

  return result;
}

Api::IoCallSizeResult MockFile::pwrite(const void* buf, uint64_t count, uint64_t offset) {
  absl::MutexLock lock(&mutex_);
  if (!is_open_) {
    return {-1, Api::IoErrorPtr(nullptr, [](Api::IoError*) { PANIC("reached unexpected code"); })};
  }

  Api::IoCallSizeResult result = pwrite_(buf, count, offset);
  num_pwrites_++;

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
