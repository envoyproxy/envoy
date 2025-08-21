#include "envoy/common/exception.h"

#include "source/common/common/fmt.h"
#include "source/common/filesystem/directory_iterator_impl.h"

#include "absl/strings/strip.h"

namespace Envoy {
namespace Filesystem {

DirectoryIteratorImpl::DirectoryIteratorImpl(const std::string& directory_path)
    : DirectoryIterator(), find_handle_(INVALID_HANDLE_VALUE) {
  absl::string_view path = absl::StripSuffix(directory_path, "/");
  path = absl::StripSuffix(path, "\\");
  WIN32_FIND_DATA find_data;
  const std::string glob = absl::StrCat(path, "\\*");
  find_handle_ = ::FindFirstFile(glob.c_str(), &find_data);
  if (find_handle_ == INVALID_HANDLE_VALUE) {
    status_ =
        absl::UnknownError(fmt::format("unable to open directory {}: {}", path, ::GetLastError()));
  }
  if (status_.ok()) {
    entry_ = makeEntry(find_data);
  } else {
    entry_ = {"", FileType::Other, absl::nullopt};
  }
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

  if (ret == 0) {
    entry_ = {"", FileType::Other, absl::nullopt};
    if (err != ERROR_NO_MORE_FILES) {
      status_ = absl::UnknownError(fmt::format("unable to iterate directory: {}", err));
    }
  } else {
    entry_ = makeEntry(find_data);
  }

  return *this;
}

DirectoryEntry DirectoryIteratorImpl::makeEntry(const WIN32_FIND_DATA& find_data) {
  // `IO_REPARSE_FLAG_SYMLINK` must only be used in conjunction with
  // `FILE_ATTRIBUTE_REPARSE_POINT`, per documentation for `dwReserved0` at
  // https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-win32_find_dataa
  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !(find_data.dwReserved0 & IO_REPARSE_TAG_SYMLINK)) {
    // The file is reparse point and not a symlink, so it can't be
    // a regular file or a directory
    return {std::string(find_data.cFileName), FileType::Other, absl::nullopt};
  } else if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return {std::string(find_data.cFileName), FileType::Directory, absl::nullopt};
  } else if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
             (find_data.dwReserved0 & IO_REPARSE_TAG_SYMLINK)) {
    return {std::string(find_data.cFileName), FileType::Regular, absl::nullopt};
  } else {
    ULARGE_INTEGER file_size;
    file_size.LowPart = find_data.nFileSizeLow;
    file_size.HighPart = find_data.nFileSizeHigh;
    uint64_t size = static_cast<uint64_t>(file_size.QuadPart);
    return {std::string(find_data.cFileName), FileType::Regular, size};
  }
}

} // namespace Filesystem
} // namespace Envoy
