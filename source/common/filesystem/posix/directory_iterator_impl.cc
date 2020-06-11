#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::DirectoryIteratorImpl(const std::string& directory_path)
    : directory_path_(directory_path), dir_(nullptr),
      os_sys_calls_(Api::OsSysCallsSingleton::get()) {
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
    entry_ = {"", FileType::Other};
  } else {
    const std::string current_path(entry->d_name);
    const std::string full_path(directory_path_ + "/" + current_path);
    entry_ = {current_path, fileType(full_path)};
  }
}

FileType DirectoryIteratorImpl::fileType(const std::string& full_path) const {
  struct stat stat_buf;

  const Api::SysCallIntResult result = os_sys_calls_.stat(full_path.c_str(), &stat_buf);
  if (result.rc_ != 0) {
    if (errno == ENOENT) {
      // Special case. This directory entity is likely to be a symlink,
      // but the reference is broken as the target could not be stat()'ed.
      // If we confirm this with an lstat, treat this file entity as
      // a regular file, which may be unlink()'ed.
      if (::lstat(full_path.c_str(), &stat_buf) == 0 && S_ISLNK(stat_buf.st_mode)) {
        return FileType::Regular;
      }
    }
    throw EnvoyException(fmt::format("unable to stat file: '{}' ({})", full_path, errno));
  }

  if (S_ISDIR(stat_buf.st_mode)) {
    return FileType::Directory;
  } else if (S_ISREG(stat_buf.st_mode)) {
    return FileType::Regular;
  }

  return FileType::Other;
}

} // namespace Filesystem
} // namespace Envoy
