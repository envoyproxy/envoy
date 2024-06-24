#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
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
    // Explicit version of close because virtual calls in destructors are ambiguous.
    const Api::IoCallBoolResult result = FileImplPosix::close();
    ASSERT(result.return_value_);
  }
}

TmpFileImplPosix::~TmpFileImplPosix() {
  if (isOpen()) {
    // Explicit version of close because virtual calls in destructors are ambiguous.
    // (We have to duplicate the destructor to ensure the overridden close is called.)
    const Api::IoCallBoolResult result = TmpFileImplPosix::close();
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

Api::IoCallBoolResult TmpFileImplPosix::open(FlagSet in) {
  if (isOpen()) {
    return resultSuccess(true);
  }

  const auto flags_and_mode = translateFlag(in);
#ifdef O_TMPFILE
  // Try to create a temp file with no name. Only some file systems support this.
  fd_ =
      ::open(path().c_str(), (flags_and_mode.flags_ & ~O_CREAT) | O_TMPFILE, flags_and_mode.mode_);
  if (fd_ != -1) {
    return resultSuccess(true);
  }
#endif
  // If we couldn't do a nameless temp file, open a named temp file.
  return openNamedTmpFile(flags_and_mode);
}

Api::IoCallBoolResult TmpFileImplPosix::openNamedTmpFile(FlagsAndMode flags_and_mode,
                                                         bool with_unlink) {
  for (int tries = 5; tries > 0; tries--) {
    std::string try_path = generateTmpFilePath(path());
    fd_ = ::open(try_path.c_str(), flags_and_mode.flags_, flags_and_mode.mode_);
    if (fd_ != -1) {
      // Try to unlink the temp file while it's still open. Again this only works on
      // a (different) subset of file systems.
      if (!with_unlink || ::unlink(try_path.c_str()) != 0) {
        // If we couldn't unlink it, set tmp_file_path_, to unlink after close.
        tmp_file_path_ = try_path;
      }
      return resultSuccess(true);
    }
  }
  return resultFailure(false, errno);
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

Api::IoCallBoolResult TmpFileImplPosix::close() {
  ASSERT(isOpen());
  int rc = ::close(fd_);
  fd_ = -1;
  int rc2 = tmp_file_path_.empty() ? 0 : ::unlink(tmp_file_path_.c_str());
  return (rc != -1 && rc2 != -1) ? resultSuccess(true) : resultFailure(false, errno);
}

Api::IoCallSizeResult FileImplPosix::pread(void* buf, uint64_t count, uint64_t offset) {
  ASSERT(isOpen());
  ssize_t rc = ::pread(fd_, buf, count, offset);
  return (rc == -1) ? resultFailure(rc, errno) : resultSuccess(rc);
}

Api::IoCallSizeResult FileImplPosix::pwrite(const void* buf, uint64_t count, uint64_t offset) {
  ASSERT(isOpen());
  ssize_t rc = ::pwrite(fd_, buf, count, offset);
  return (rc == -1) ? resultFailure(rc, errno) : resultSuccess(rc);
}

static FileType typeFromStat(const struct stat& s) {
  if (S_ISDIR(s.st_mode)) {
    return FileType::Directory;
  }
  if (S_ISREG(s.st_mode)) {
    return FileType::Regular;
  }
  return FileType::Other;
}

static constexpr absl::optional<SystemTime> systemTimeFromTimespec(const struct timespec& t) {
  if (t.tv_sec == 0) {
    return absl::nullopt;
  }
  return timespecToChrono(t);
}

static Api::IoCallResult<FileInfo> infoFromStat(absl::string_view path, const struct stat& s,
                                                FileType type) {
  auto result_or_error = InstanceImplPosix().splitPathFromFilename(path);
  FileInfo ret = {
      result_or_error.status().ok() ? std::string{result_or_error.value().file_} : "",
      s.st_size,
      type,
#ifdef _DARWIN_FEATURE_64_BIT_INODE
      systemTimeFromTimespec(s.st_ctimespec),
      systemTimeFromTimespec(s.st_atimespec),
      systemTimeFromTimespec(s.st_mtimespec),
#else
      systemTimeFromTimespec(s.st_ctim),
      systemTimeFromTimespec(s.st_atim),
      systemTimeFromTimespec(s.st_mtim),
#endif
  };
  return result_or_error.status().ok() ? resultSuccess(ret) : resultFailure(ret, EINVAL);
}

static Api::IoCallResult<FileInfo> infoFromStat(absl::string_view path, const struct stat& s) {
  return infoFromStat(path, s, typeFromStat(s)); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
}

Api::IoCallResult<FileInfo> FileImplPosix::info() {
  ASSERT(isOpen());
  struct stat s;
  if (::fstat(fd_, &s) != 0) {
    return resultFailure<FileInfo>({}, errno);
  }
  return infoFromStat(path(), s);
}

Api::IoCallResult<FileInfo> InstanceImplPosix::stat(absl::string_view path) {
  struct stat s;
  std::string full_path{path};
  if (::stat(full_path.c_str(), &s) != 0) {
    if (errno == ENOENT) {
      if (::lstat(full_path.c_str(), &s) == 0 && S_ISLNK(s.st_mode)) {
        // Special case. This directory entity is a symlink,
        // but the reference is broken as the target could not be stat()'ed.
        // After confirming this with an lstat, treat this file entity as
        // a regular file, which may be unlink()'ed.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        return infoFromStat(path, s, FileType::Regular);
      }
    }
    return resultFailure<FileInfo>({}, errno);
  }
  return infoFromStat(path, s);
}

Api::IoCallBoolResult InstanceImplPosix::createPath(absl::string_view path) {
  // Ideally we could just use std::filesystem::create_directories for this,
  // identical to ../win32/filesystem_impl.cc, but the OS version used in mobile
  // CI doesn't support it, so we have to do recursive path creation manually.
  while (!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  if (directoryExists(std::string{path})) {
    return resultSuccess(false);
  }
  absl::string_view subpath = path;
  do {
    size_t slashpos = subpath.find_last_of('/');
    if (slashpos == absl::string_view::npos) {
      return resultFailure(false, ENOENT);
    }
    subpath = subpath.substr(0, slashpos);
  } while (!directoryExists(std::string{subpath}));
  // We're now at the most-nested directory that already exists.
  // Time to create paths recursively.
  while (subpath != path) {
    size_t slashpos = path.find_first_of('/', subpath.size() + 2);
    subpath = (slashpos == absl::string_view::npos) ? path : path.substr(0, slashpos);
    std::string dir_to_create{subpath};
    if (mkdir(dir_to_create.c_str(), 0777)) {
      return resultFailure(false, errno);
    }
  }
  return resultSuccess(true);
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
  } else if (in.test(File::Operation::Write) && !in.test(File::Operation::KeepExistingData)) {
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
  case DestinationType::TmpFile:
    if (!file_info.path_.empty() && file_info.path_.back() == '/') {
      return std::make_unique<TmpFileImplPosix>(FilePathAndType{
          DestinationType::TmpFile, file_info.path_.substr(0, file_info.path_.size() - 1)});
    }
    return std::make_unique<TmpFileImplPosix>(file_info);
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

absl::StatusOr<std::string> InstanceImplPosix::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid path: ", path));
  }

  std::ifstream file(path);
  if (file.fail()) {
    return absl::InvalidArgumentError(absl::StrCat("unable to read file: ", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
}

absl::StatusOr<PathSplitResult> InstanceImplPosix::splitPathFromFilename(absl::string_view path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    return absl::InvalidArgumentError(fmt::format("invalid file path {}", path));
  }
  absl::string_view name = path.substr(last_slash + 1);
  // truncate all trailing slashes, except root slash
  if (last_slash == 0) {
    ++last_slash;
  }
  return PathSplitResult{path.substr(0, last_slash), name};
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
  // It will allow the canonical path such as /sysroot/ which is not the
  // default reserved directories (/dev, /sys, /proc)
  if (absl::StartsWith(canonical_path.return_value_, "/dev/") ||
      absl::StartsWith(canonical_path.return_value_, "/sys/") ||
      absl::StartsWith(canonical_path.return_value_, "/proc/") ||
      canonical_path.return_value_ == "/dev" || canonical_path.return_value_ == "/sys" ||
      canonical_path.return_value_ == "/proc") {
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
