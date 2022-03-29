#pragma once

#include <sys/stat.h>
#include <unistd.h>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// A wrapper around the file operations used by the async files library,
// to allow us to mock the operations for testing.
//
// The default implementation is a simple pass-through to the posix functions.
class PosixFileOperations {
public:
  virtual ~PosixFileOperations() = default;
  virtual int open(const char* pathname, int flags) const PURE;
  virtual int open(const char* pathname, int flags, mode_t mode) const PURE;
  virtual int close(int fd) const PURE;
  virtual ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) const PURE;
  virtual ssize_t pread(int fd, void* buf, size_t count, off_t offset) const PURE;
  virtual int unlink(const char* pathname) const PURE;
  virtual int dup(int fd) const PURE;
  virtual int linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath,
                     int flags) const PURE;
  virtual int mkstemp(char* tmplate) const PURE;
};

const PosixFileOperations& realPosixFileOperations();

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
