#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"

#undef DELETE
#undef GetMessage

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/fmt.h"

namespace Envoy {
namespace Filesystem {

bool InstanceImplWin32::fileExists(const std::string& path) {
  const DWORD attributes = ::GetFileAttributes(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES;
}

bool InstanceImplWin32::directoryExists(const std::string& path) {
  const DWORD attributes = ::GetFileAttributes(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return attributes & FILE_ATTRIBUTE_DIRECTORY;
}

ssize_t InstanceImplWin32::fileSize(const std::string& path) {
  struct _stat info;
  if (::_stat(path.c_str(), &info) != 0) {
    return -1;
  }
  return info.st_size;
}

std::string InstanceImplWin32::fileReadToEnd(const std::string& path) {
  std::ios::sync_with_stdio(false);

  // On Windows, we need to explicitly set the file mode as binary. Otherwise,
  // 0x1a will be treated as EOF
  std::ifstream file(path, std::ios_base::binary);
  if (!file) {
    throw EnvoyException(fmt::format("unable to read file: {}", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

bool InstanceImplWin32::illegalPath(const std::string& path) {
  // Currently, we don't know of any obviously illegal paths on Windows
  return false;
}

FilePtr InstanceImplWin32::createFile(const std::string& path) {
  return std::make_unique<FileImplWin32>(path);
}

FileImplWin32::FileImplWin32(const std::string& path) : path_(path) { open(); }

FileImplWin32::~FileImplWin32() { close(); }

void FileImplWin32::open() {
  const int flags = _O_RDWR | _O_APPEND | _O_CREAT;
  const int mode = _S_IREAD | _S_IWRITE;

  fd_ = ::_open(path_.c_str(), flags, mode);
  if (-1 == fd_) {
    throw EnvoyException(fmt::format("unable to open file '{}': {}", path_, strerror(errno)));
  }
}

Api::SysCallSizeResult FileImplWin32::write(const void* buffer, size_t len) {
  if (fd_ == -1) {
    return {-1, EBADF};
  }

  const ssize_t rc = ::_write(fd_, buffer, len);
  return {rc, errno};
}

void FileImplWin32::close() {
  if (fd_ == -1) {
    return;
  }
  const int rc = ::_close(fd_);
  ASSERT(rc == 0);
  fd_ = -1;
}

bool FileImplWin32::isOpen() { return fd_ != -1; }

} // namespace Filesystem
} // namespace Envoy
