#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/directory_iterator_impl.h"

#include "absl/strings/strip.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::DirectoryIteratorImpl(const std::string& directory_path)
    : directory_path_(absl::StripSuffix(directory_path, "/")),
      os_sys_calls_(Api::OsSysCallsSingleton::get()) {
  openDirectory();
  if (status_.ok()) {
    nextEntry();
  } else {
    entry_ = {"", FileType::Other, absl::nullopt};
  }
}

DirectoryIteratorImpl::~DirectoryIteratorImpl() {
  if (dir_ != nullptr) {
    ::closedir(dir_);
  }
}

DirectoryIteratorImpl& DirectoryIteratorImpl::operator++() {
  nextEntry();
  return *this;
}

void DirectoryIteratorImpl::openDirectory() {
  DIR* temp_dir = ::opendir(directory_path_.c_str());
  dir_ = temp_dir;
  if (!dir_) {
    status_ = absl::ErrnoToStatus(errno, fmt::format("unable to open directory {}: {}",
                                                     directory_path_, errorDetails(errno)));
  }
}

void DirectoryIteratorImpl::nextEntry() {
  errno = 0;
  dirent* entry = ::readdir(dir_);

  if (entry == nullptr) {
    entry_ = {"", FileType::Other, absl::nullopt};
    if (errno != 0) {
      status_ = absl::ErrnoToStatus(errno, fmt::format("unable to iterate directory {}: {}",
                                                       directory_path_, errorDetails(errno)));
    }
  } else {
    auto result = makeEntry(entry->d_name);
    status_ = result.status();
    if (!status_.ok()) {
      // If we failed to stat one file it might have just been deleted,
      // keep iterating over the rest of the dir.
      return nextEntry();
    } else {
      entry_ = result.value();
    }
  }
}

absl::StatusOr<DirectoryEntry> DirectoryIteratorImpl::makeEntry(absl::string_view filename) const {
  const std::string full_path = absl::StrCat(directory_path_, "/", filename);
  struct stat stat_buf;
  const Api::SysCallIntResult result = os_sys_calls_.stat(full_path.c_str(), &stat_buf);
  if (result.return_value_ != 0) {
    if (result.errno_ == ENOENT) {
      // Special case. This directory entity is likely to be a symlink,
      // but the reference is broken as the target could not be stat()'ed.
      // If we confirm this with an lstat, treat this file entity as
      // a regular file, which may be unlink()'ed.
      if (::lstat(full_path.c_str(), &stat_buf) == 0 && S_ISLNK(stat_buf.st_mode)) {
        return DirectoryEntry{std::string{filename}, FileType::Regular, absl::nullopt};
      }
    }
    return absl::ErrnoToStatus(
        result.errno_, fmt::format("unable to stat file: '{}' ({})", full_path, result.errno_));
  } else if (S_ISDIR(stat_buf.st_mode)) {
    return DirectoryEntry{std::string{filename}, FileType::Directory, absl::nullopt};
  } else if (S_ISREG(stat_buf.st_mode)) {
    return DirectoryEntry{std::string{filename}, FileType::Regular,
                          static_cast<uint64_t>(stat_buf.st_size)};
  } else {
    return DirectoryEntry{std::string{filename}, FileType::Other, absl::nullopt};
  }
}

} // namespace Filesystem
} // namespace Envoy
