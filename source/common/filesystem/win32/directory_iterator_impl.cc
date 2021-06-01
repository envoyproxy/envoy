#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::DirectoryIteratorImpl(const std::string& directory_path)
    : DirectoryIterator(), find_handle_(INVALID_HANDLE_VALUE) {
  WIN32_FIND_DATA find_data;
  const std::string glob = directory_path + "\\*";
  find_handle_ = ::FindFirstFile(glob.c_str(), &find_data);
  if (find_handle_ == INVALID_HANDLE_VALUE) {
    throw EnvoyException(
        fmt::format("unable to open directory {}: {}", directory_path, ::GetLastError()));
  }

  entry_ = {std::string(find_data.cFileName), fileType(find_data)};
}

DirectoryIteratorImpl::~DirectoryIteratorImpl() {
  if (find_handle_ != INVALID_HANDLE_VALUE) {
    ::FindClose(find_handle_);
  }
}

DirectoryIteratorImpl& DirectoryIteratorImpl::operator++() {
  WIN32_FIND_DATA find_data;
  const BOOL ret = ::FindNextFile(find_handle_, &find_data);
  const DWORD err = ::GetLastError();
  if (ret == 0 && err != ERROR_NO_MORE_FILES) {
    throw EnvoyException(fmt::format("unable to iterate directory: {}", err));
  }

  if (ret == 0) {
    entry_ = {"", FileType::Other};
  } else {
    entry_ = {std::string(find_data.cFileName), fileType(find_data)};
  }

  return *this;
}

FileType DirectoryIteratorImpl::fileType(const WIN32_FIND_DATA& find_data) const {
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
