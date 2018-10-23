#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::~DirectoryIteratorImpl() {
  if (find_handle_ != INVALID_HANDLE_VALUE) {
    ::FindClose(find_handle_);
  }
}

DirectoryIterator::DirectoryEntry DirectoryIteratorImpl::nextEntry() {
  if (find_handle_ == INVALID_HANDLE_VALUE) {
    return firstEntry();
  }

  WIN32_FIND_DATA find_data;
  const BOOL ret = ::FindNextFile(find_handle_, &find_data);
  const DWORD err = ::GetLastError();
  if (ret == 0 && err != ERROR_NO_MORE_FILES) {
    throw EnvoyException(fmt::format("unable to iterate directory {}: {}", directory_path_, err));
  }

  if (ret == 0) {
    return {"", FileType::Other};
  }

  return {std::string(find_data.cFileName), fileType(find_data)};
}

DirectoryIterator::DirectoryEntry DirectoryIteratorImpl::firstEntry() {
  WIN32_FIND_DATA find_data;
  std::string glob = directory_path_ + "\\*";
  find_handle_ = ::FindFirstFile(glob.c_str(), &find_data);
  if (find_handle_ == INVALID_HANDLE_VALUE) {
    throw EnvoyException(
        fmt::format("unable to open directory {}: {}", directory_path_, ::GetLastError()));
  }

  return {std::string(find_data.cFileName), fileType(find_data)};
}

DirectoryIterator::FileType
DirectoryIteratorImpl::fileType(const WIN32_FIND_DATA& find_data) const {
  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !(find_data.dwReserved0 & IO_REPARSE_TAG_SYMLINK)) {
    // The file is reparse point and not a symlink, so it can't be
    // a regular file or a directory
    return FileType::Other;
  }

  if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return FileType::Directory;
  }

  return FileType::Regular;
}

} // namespace Filesystem
} // namespace Envoy
