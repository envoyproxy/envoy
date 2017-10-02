#include "common/api/os_sys_calls_impl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Envoy {
namespace Api {

int OsSysCallsImpl::open(const std::string& full_path, int flags, int mode) {
  return ::open(full_path.c_str(), flags, mode);
}

int OsSysCallsImpl::close(int fd) { return ::close(fd); }

ssize_t OsSysCallsImpl::write(int fd, const void* buffer, size_t num_bytes) {
  return ::write(fd, buffer, num_bytes);
}

} // namespace Api
} // namespace Envoy
