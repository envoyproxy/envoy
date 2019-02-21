#include "common/filesystem/filesystem_impl.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Filesystem {

FileImpl::FileImpl(const std::string& path) : fd_(-1), path_(path) {}

FileImpl::~FileImpl() {
  if (isOpen()) {
    const Api::SysCallBoolResult result = close();
    ASSERT(result.rc_);
  }
}

Api::SysCallBoolResult FileImpl::open() {
  if (isOpen()) {
    return {true, 0};
  }

  const int flags = O_RDWR | O_APPEND | O_CREAT;
  const int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  fd_ = ::open(path_.c_str(), flags, mode);
  if (-1 == fd_) {
    return {false, errno};
  }
  return {true, 0};
}

Api::SysCallSizeResult FileImpl::write(absl::string_view buffer) {
  const ssize_t rc = ::write(fd_, buffer.data(), buffer.size());
  return {rc, errno};
}

Api::SysCallBoolResult FileImpl::close() {
  ASSERT(isOpen());

  const int rc = ::close(fd_);
  if (rc == -1) {
    return {false, errno};
  }

  fd_ = -1;
  return {true, 0};
}

bool FileImpl::isOpen() { return fd_ != -1; }

std::string FileImpl::path() { return path_; }

std::string FileImpl::errorToString(int error) { return ::strerror(error); }

FilePtr InstanceImpl::createFile(const std::string& path) {
  return std::make_unique<FileImpl>(path);
}

bool InstanceImpl::fileExists(const std::string& path) {
  std::ifstream input_file(path);
  return input_file.is_open();
}

bool InstanceImpl::directoryExists(const std::string& path) {
  DIR* const dir = ::opendir(path.c_str());
  const bool dir_exists = nullptr != dir;
  if (dir_exists) {
    ::closedir(dir);
  }

  return dir_exists;
}

ssize_t InstanceImpl::fileSize(const std::string& path) {
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) {
    return -1;
  }
  return info.st_size;
}

std::string InstanceImpl::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    throw EnvoyException(fmt::format("Invalid path: {}", path));
  }

  std::ios::sync_with_stdio(false);

  std::ifstream file(path);
  if (!file) {
    throw EnvoyException(fmt::format("unable to read file: {}", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

Api::SysCallStringResult InstanceImpl::canonicalPath(const std::string& path) {
  // TODO(htuch): When we are using C++17, switch to std::filesystem::canonical.
  char* resolved_path = ::realpath(path.c_str(), nullptr);
  if (resolved_path == nullptr) {
    return {std::string(), errno};
  }
  std::string resolved_path_string{resolved_path};
  ::free(resolved_path);
  return {resolved_path_string, 0};
}

bool InstanceImpl::illegalPath(const std::string& path) {
  const Api::SysCallStringResult canonical_path = canonicalPath(path);
  if (canonical_path.rc_.empty()) {
    ENVOY_LOG_MISC(debug, "Unable to determine canonical path for {}: {}", path,
                   ::strerror(canonical_path.errno_));
    return true;
  }

  // Platform specific path sanity; we provide a convenience to avoid Envoy
  // instances poking in bad places. We may have to consider conditioning on
  // platform in the future, growing these or relaxing some constraints (e.g.
  // there are valid reasons to go via /proc for file paths).
  // TODO(htuch): Optimize this as a hash lookup if we grow any further.
  if (absl::StartsWith(canonical_path.rc_, "/dev") ||
      absl::StartsWith(canonical_path.rc_, "/sys") ||
      absl::StartsWith(canonical_path.rc_, "/proc")) {
    return true;
  }
  return false;
}

} // namespace Filesystem
} // namespace Envoy
