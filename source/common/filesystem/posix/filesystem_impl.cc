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

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Filesystem {

FileImplPosix::~FileImplPosix() {
  if (isOpen()) {
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    const Api::IoCallBoolResult result = close();
    ASSERT(result.return_value_);
  }
}

Api::IoCallBoolResult FileImplPosix::open(FlagSet in) {
  if (isOpen()) {
    return resultSuccess(true);
  }

  const auto flags_and_mode = translateFlag(in);
  fd_ = ::open(path().c_str(), flags_and_mode.flags_, flags_and_mode.mode_);
  return fd_ != -1 ? resultSuccess(true) : resultFailure(false, errno);
}

Api::IoCallSizeResult FileImplPosix::write(absl::string_view buffer) {
  const ssize_t rc = ::write(fd_, buffer.data(), buffer.size());
  return rc != -1 ? resultSuccess(rc) : resultFailure(rc, errno);
};

Api::IoCallBoolResult FileImplPosix::close() {
  ASSERT(isOpen());
  int rc = ::close(fd_);
  fd_ = -1;
  return (rc != -1) ? resultSuccess(true) : resultFailure(false, errno);
}

FileImplPosix::FlagsAndMode FileImplPosix::translateFlag(FlagSet in) {
  int out = 0;
  mode_t mode = 0;
  if (in.test(File::Operation::Create)) {
    out |= O_CREAT;
    mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  }

  if (in.test(File::Operation::Append)) {
    out |= O_APPEND;
  } else if (in.test(File::Operation::Write)) {
    out |= O_TRUNC;
  }

  if (in.test(File::Operation::Read) && in.test(File::Operation::Write)) {
    out |= O_RDWR;
  } else if (in.test(File::Operation::Read)) {
    out |= O_RDONLY;
  } else if (in.test(File::Operation::Write)) {
    out |= O_WRONLY;
  }

  return {out, mode};
}

FilePtr InstanceImplPosix::createFile(const FilePathAndType& file_info) {
  switch (file_info.file_type_) {
  case DestinationType::File:
    return std::make_unique<FileImplPosix>(file_info);
  case DestinationType::Stderr:
    return std::make_unique<FileImplPosix>(FilePathAndType{DestinationType::Stderr, "/dev/stderr"});
  case DestinationType::Stdout:
    return std::make_unique<FileImplPosix>(FilePathAndType{DestinationType::Stdout, "/dev/stdout"});
  }
  return nullptr; // for gcc
}

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
  if (illegalPath(path)) {
    throw EnvoyException(absl::StrCat("Invalid path: ", path));
  }

  std::ifstream file(path);
  if (file.fail()) {
    throw EnvoyException(absl::StrCat("unable to read file: ", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

PathSplitResult InstanceImplPosix::splitPathFromFilename(absl::string_view path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    throw EnvoyException(fmt::format("invalid file path {}", path));
  }
  absl::string_view name = path.substr(last_slash + 1);
  // truncate all trailing slashes, except root slash
  if (last_slash == 0) {
    ++last_slash;
  }
  return {path.substr(0, last_slash), name};
}

bool InstanceImplPosix::illegalPath(const std::string& path) {
  // Special case, allow /dev/fd/* access here so that config can be passed in a
  // file descriptor from a bootstrap script via exec. The reason we do this
  // _before_ canonicalizing the path is that different unix flavors implement
  // /dev/fd/* differently, for example on linux they are symlinks to /dev/pts/*
  // which are symlinks to /proc/self/fds/. On BSD (and darwin) they are not
  // symlinks at all. To avoid lots of platform, specifics, we allowlist
  // /dev/fd/* _before_ resolving the canonical path.
  if (absl::StartsWith(path, "/dev/fd/")) {
    return false;
  }

  const Api::SysCallStringResult canonical_path = canonicalPath(path);
  if (canonical_path.return_value_.empty()) {
    ENVOY_LOG_MISC(debug, "Unable to determine canonical path for {}: {}", path,
                   errorDetails(canonical_path.errno_));
    return true;
  }

  // Platform specific path sanity; we provide a convenience to avoid Envoy
  // instances poking in bad places. We may have to consider conditioning on
  // platform in the future, growing these or relaxing some constraints (e.g.
  // there are valid reasons to go via /proc for file paths).
  // TODO(htuch): Optimize this as a hash lookup if we grow any further.
  if (absl::StartsWith(canonical_path.return_value_, "/dev") ||
      absl::StartsWith(canonical_path.return_value_, "/sys") ||
      absl::StartsWith(canonical_path.return_value_, "/proc")) {
    return true;
  }
  return false;
}

Api::SysCallStringResult InstanceImplPosix::canonicalPath(const std::string& path) {
  char* resolved_path = ::realpath(path.c_str(), nullptr);
  if (resolved_path == nullptr) {
    return {std::string(), errno};
  }
  std::string resolved_path_string{resolved_path};
  ::free(resolved_path);
  return {resolved_path_string, 0};
}

} // namespace Filesystem
} // namespace Envoy
