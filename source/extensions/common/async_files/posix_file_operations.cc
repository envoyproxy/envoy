#include "source/extensions/common/async_files/posix_file_operations.h"

#include <fcntl.h>

#include <cstdlib>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class PosixFileOperationsImpl : public PosixFileOperations {
public:
  int open(const char* pathname, int flags) const override { return ::open(pathname, flags); }
  int open(const char* pathname, int flags, mode_t mode) const override {
    return ::open(pathname, flags, mode);
  }
  int close(int fd) const override { return ::close(fd); }
  ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) const override {
    return ::pwrite(fd, buf, count, offset);
  }
  ssize_t pread(int fd, void* buf, size_t count, off_t offset) const override {
    return ::pread(fd, buf, count, offset);
  }
  int unlink(const char* pathname) const override { return ::unlink(pathname); }
  int dup(int fd) const override { return ::dup(fd); }
  int linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath,
             int flags) const override {
    return ::linkat(olddirfd, oldpath, newdirfd, newpath, flags);
  }
  int mkstemp(char* tmplate) const override { return ::mkstemp(tmplate); }
};

const PosixFileOperations& realPosixFileOperations() {
  static const PosixFileOperationsImpl real;
  return real;
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
