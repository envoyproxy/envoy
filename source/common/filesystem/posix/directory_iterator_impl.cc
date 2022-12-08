#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::DirectoryIteratorImpl(const std::string& directory_path)
    : directory_path_(directory_path), os_sys_calls_(Api::OsSysCallsSingleton::get()) {
  openDirectory();
  nextEntry();
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
    throw EnvoyException(
        fmt::format("unable to open directory {}: {}", directory_path_, errorDetails(errno)));
  }
}

void DirectoryIteratorImpl::nextEntry() {
  errno = 0;
  dirent* entry = ::readdir(dir_);
  if (entry == nullptr && errno != 0) {
    throw EnvoyException(
        fmt::format("unable to iterate directory {}: {}", directory_path_, errorDetails(errno)));
  }

  if (entry == nullptr) {
    entry_ = {"", FileType::Other, absl::nullopt};
  } else {
    entry_ = makeEntry(entry->d_name);
  }
}

DirectoryEntry DirectoryIteratorImpl::makeEntry(absl::string_view filename) const {
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
    // TODO: throwing an exception here makes this dangerous to use in worker threads,
    // and in general since it's not clear to the user of Directory that an exception
    // may be thrown. Perhaps make this return StatusOr and handle failures gracefully.
    throw EnvoyException(fmt::format("unable to stat file: '{}' ({})", full_path, errno));
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
