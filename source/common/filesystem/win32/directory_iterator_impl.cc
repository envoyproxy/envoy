#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/filesystem/directory_iterator_impl.h"

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

  entry_ = makeEntry(find_data);
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
    entry_ = {"", FileType::Other, 0};
  } else {
    entry_ = makeEntry(find_data);
  }

  return *this;
}

DirectoryEntry DirectoryIteratorImpl::makeEntry(const WIN32_FIND_DATA& find_data) {
  uint64_t size = static_cast<uint64_t>(find_data.nFileSizeHigh) << 32 + find_data.nFileSizeLow;
  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !(find_data.dwReserved0 & IO_REPARSE_TAG_SYMLINK)) {
    // The file is reparse point and not a symlink, so it can't be
    // a regular file or a directory
    return {std::string(find_data.cFileName), FileType::Other, size};
  } else if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return {std::string(find_data.cFileName), FileType::Directory, 0};
  } else {
    return {std::string(find_data.cFileName), FileType::Regular, size};
  }
}

} // namespace Filesystem
} // namespace Envoy
