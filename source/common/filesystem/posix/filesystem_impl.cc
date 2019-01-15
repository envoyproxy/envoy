#include <dirent.h>
#include <sys/stat.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/filesystem/filesystem_impl.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Filesystem {

bool InstanceImplPosix::fileExists(const std::string& path) {
  std::ifstream input_file(path);
  return input_file.is_open();
}

bool InstanceImplPosix::directoryExists(const std::string& path) {
  DIR* const dir = ::opendir(path.c_str());
  const bool dir_exists = nullptr != dir;
  if (dir_exists) {
    ::closedir(dir);
  }

  return dir_exists;
}

ssize_t InstanceImplPosix::fileSize(const std::string& path) {
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) {
    return -1;
  }
  return info.st_size;
}

std::string InstanceImplPosix::fileReadToEnd(const std::string& path) {
  std::ios::sync_with_stdio(false);

  std::ifstream file(path);
  if (!file) {
    throw EnvoyException(fmt::format("unable to read file: {}", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

bool InstanceImplPosix::illegalPath(const std::string& path) {
  try {
    const std::string canonical_path = canonicalPath(path);
    // Platform specific path sanity; we provide a convenience to avoid Envoy
    // instances poking in bad places. We may have to consider conditioning on
    // platform in the future, growing these or relaxing some constraints (e.g.
    // there are valid reasons to go via /proc for file paths).
    // TODO(htuch): Optimize this as a hash lookup if we grow any further.
    if (absl::StartsWith(canonical_path, "/dev") || absl::StartsWith(canonical_path, "/sys") ||
        absl::StartsWith(canonical_path, "/proc")) {
      return true;
    }
    return false;
  } catch (const EnvoyException& ex) {
    ENVOY_LOG_MISC(debug, "Unable to determine canonical path for {}: {}", path, ex.what());
    return true;
  }
}

FilePtr InstanceImplPosix::createFile(const std::string& path) {
  return std::make_unique<FileImplPosix>(path);
}

std::string InstanceImplPosix::canonicalPath(const std::string& path) {
  // TODO(htuch): When we are using C++17, switch to std::filesystem::canonical.
  char* resolved_path = ::realpath(path.c_str(), nullptr);
  if (resolved_path == nullptr) {
    throw EnvoyException(fmt::format("Unable to determine canonical path for {}", path));
  }
  std::string resolved_path_string{resolved_path};
  ::free(resolved_path);
  return resolved_path_string;
}

FileImplPosix::FileImplPosix(const std::string& path) : path_(path) { open(); }

FileImplPosix::~FileImplPosix() { close(); }

void FileImplPosix::open() {
  const int flags = O_RDWR | O_APPEND | O_CREAT;
  const int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  fd_ = ::open(path_.c_str(), flags, mode);
  if (-1 == fd_) {
    throw EnvoyException(fmt::format("unable to open file '{}': {}", path_, strerror(errno)));
  }
}

Api::SysCallSizeResult FileImplPosix::write(const void* buffer, size_t len) {
  const ssize_t rc = ::write(fd_, buffer, len);
  return {rc, errno};
}

void FileImplPosix::close() {
  if (fd_ == -1) {
    return;
  }
  const int rc = ::close(fd_);
  ASSERT(rc == 0);
  fd_ = -1;
}

bool FileImplPosix::isOpen() { return fd_ != -1; }

} // namespace Filesystem
} // namespace Envoy
