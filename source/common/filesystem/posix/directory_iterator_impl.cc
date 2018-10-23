#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::~DirectoryIteratorImpl() {
  if (dir_ != nullptr) {
    ::closedir(dir_);
  }
}

DirectoryIterator::DirectoryEntry DirectoryIteratorImpl::nextEntry() {
  if (dir_ == nullptr) {
    openDirectory();
  }

  errno = 0;
  dirent* entry = ::readdir(dir_);
  if (entry == nullptr && errno != 0) {
    throw EnvoyException(fmt::format("unable to iterate directory: {}", directory_path_));
  }

  if (entry == nullptr) {
    return {"", FileType::Other};
  }

  const std::string current_path = std::string(entry->d_name);
  const std::string full_path = directory_path_ + "/" + current_path;
  return {current_path, fileType(full_path)};
}

void DirectoryIteratorImpl::openDirectory() {
  dir_ = ::opendir(directory_path_.c_str());
  if (!dir_) {
    throw EnvoyException(
        fmt::format("unable to open directory {}: {}", directory_path_, strerror(errno)));
  }
}

DirectoryIterator::FileType DirectoryIteratorImpl::fileType(const std::string& full_path) const {
  struct stat stat_buf;

  const Api::SysCallIntResult result = os_sys_calls_.stat(full_path.c_str(), &stat_buf);
  if (result.rc_ != 0) {
    throw EnvoyException(fmt::format("unable to stat file: '{}'", full_path));
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
