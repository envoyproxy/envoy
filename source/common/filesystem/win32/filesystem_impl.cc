#include <fcntl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/filesystem_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Filesystem {

FileImplWin32::~FileImplWin32() {
  if (isOpen()) {
    const Api::IoCallBoolResult result = close();
    ASSERT(result.return_value_);
  }
}

Api::IoCallBoolResult FileImplWin32::open(FlagSet in) {
  if (isOpen()) {
    return resultSuccess(true);
  }

  auto flags = translateFlag(in);
  fd_ = CreateFileA(path().c_str(), flags.access_,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, flags.creation_, 0,
                    NULL);
  if (fd_ == INVALID_HANDLE) {
    return resultFailure(false, ::GetLastError());
  }
  if (in.test(File::Operation::Write) && !in.test(File::Operation::Append) &&
      !in.test(File::Operation::KeepExistingData)) {
    SetEndOfFile(fd_);
  }
  return resultSuccess(true);
}

Api::IoCallBoolResult TmpFileImplWin32::open(FlagSet in) {
  if (isOpen()) {
    return resultSuccess(true);
  }

  auto flags = translateFlag(in);
  for (int tries = 5; tries > 0; tries--) {
    std::string try_path = generateTmpFilePath(path());
    fd_ = CreateFileA(try_path.c_str(), flags.access_,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_NEW,
                      FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (fd_ != INVALID_HANDLE) {
      tmp_file_path_ = try_path;
      return resultSuccess(true);
    }
  }
  return resultFailure(false, ::GetLastError());
}

Api::IoCallSizeResult FileImplWin32::write(absl::string_view buffer) {
  DWORD bytes_written;
  BOOL result = WriteFile(fd_, buffer.data(), buffer.length(), &bytes_written, NULL);
  if (result == 0) {
    return resultFailure<ssize_t>(-1, ::GetLastError());
  }
  return resultSuccess<ssize_t>(bytes_written);
};

Api::IoCallBoolResult FileImplWin32::close() {
  ASSERT(isOpen());

  BOOL result = CloseHandle(fd_);
  fd_ = INVALID_HANDLE;
  if (result == 0) {
    return resultFailure(false, ::GetLastError());
  }
  return resultSuccess(true);
}

static OVERLAPPED overlappedForOffset(uint64_t offset) {
  OVERLAPPED overlapped{};
  overlapped.Offset = offset & 0xffffffff;
  overlapped.OffsetHigh = offset >> 32;
  return overlapped;
}

Api::IoCallSizeResult FileImplWin32::pread(void* buf, uint64_t count, uint64_t offset) {
  ASSERT(isOpen());
  DWORD read_count;
  OVERLAPPED overlapped = overlappedForOffset(offset);
  BOOL result = ReadFile(fd_, buf, count, &read_count, &overlapped);
  return result ? resultSuccess(static_cast<ssize_t>(read_count))
                : resultFailure<ssize_t>(-1, ::GetLastError());
}

Api::IoCallSizeResult FileImplWin32::pwrite(const void* buf, uint64_t count, uint64_t offset) {
  ASSERT(isOpen());
  DWORD write_count;
  OVERLAPPED overlapped = overlappedForOffset(offset);
  BOOL result = WriteFile(fd_, buf, count, &write_count, &overlapped);
  return result ? resultSuccess(static_cast<ssize_t>(write_count))
                : resultFailure<ssize_t>(-1, ::GetLastError());
}

static uint64_t fileSizeFromAttributeData(const WIN32_FILE_ATTRIBUTE_DATA& data) {
  ULARGE_INTEGER file_size;
  file_size.LowPart = data.nFileSizeLow;
  file_size.HighPart = data.nFileSizeHigh;
  return static_cast<uint64_t>(file_size.QuadPart);
}

static absl::optional<SystemTime> systemTimeFromFileTime(const FILETIME& t) {
  // `FILETIME` is a 64 bit value representing the number of 100-nanosecond
  // intervals since January 1, 1601 (UTC).
  // https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
  // So we set a SystemTime to that moment, and add that many 100-nanosecond units to that
  // time, to get a SystemTime representing the same moment as the `FILETIME`, in microsecond
  // precision.
  // For timestamps earlier than the unix epoch (1970, `SystemTime{0}`), we assume it's an
  // invalid value and return nullopt.
  static const SystemTime windows_file_time_epoch =
      absl::ToChronoTime(absl::FromCivil(absl::CivilYear(1601), absl::UTCTimeZone()));
  ULARGE_INTEGER tenths_of_microseconds{t.dwLowDateTime, t.dwHighDateTime};
  uint64_t v = static_cast<uint64_t>(tenths_of_microseconds.QuadPart);
  SystemTime ret = windows_file_time_epoch + std::chrono::microseconds{v / 10};
  if (ret <= SystemTime{}) {
    // If the timestamp is before the unix epoch, return nullopt.
    return absl::nullopt;
  }
  return ret;
}

static FileType fileTypeFromAttributeData(const WIN32_FILE_ATTRIBUTE_DATA& data) {
  if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    return FileType::Directory;
  }
  if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    return FileType::Other;
  }
  return FileType::Regular;
}

static Api::IoCallResult<FileInfo>
fileInfoFromAttributeData(absl::string_view path, const WIN32_FILE_ATTRIBUTE_DATA& data) {
  absl::optional<uint64_t> sz;
  FileType type = fileTypeFromAttributeData(data);
  if (type == FileType::Regular) {
    sz = fileSizeFromAttributeData(data);
  }
  absl::StatusOr<PathSplitResult> result_or_error = InstanceImplWin32().splitPathFromFilename(path);
  FileInfo ret{
      result_or_error.ok() ? std::string{result_or_error.value().file_} : "",
      sz,
      type,
      systemTimeFromFileTime(data.ftCreationTime),
      systemTimeFromFileTime(data.ftLastAccessTime),
      systemTimeFromFileTime(data.ftLastWriteTime),
  };
  if (result_or_error.status().ok()) {
    return resultSuccess(ret);
  }
  return resultFailure(ret, ERROR_INVALID_DATA);
}

Api::IoCallResult<FileInfo> FileImplWin32::info() {
  ASSERT(isOpen());
  WIN32_FILE_ATTRIBUTE_DATA data;
  BOOL result = GetFileAttributesEx(path().c_str(), GetFileExInfoStandard, &data);
  if (!result) {
    return resultFailure<FileInfo>({}, ::GetLastError());
  }
  return fileInfoFromAttributeData(path(), data);
}

Api::IoCallResult<FileInfo> InstanceImplWin32::stat(absl::string_view path) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  std::string full_path{path};
  BOOL result = GetFileAttributesEx(full_path.c_str(), GetFileExInfoStandard, &data);
  if (!result) {
    return resultFailure<FileInfo>({}, ::GetLastError());
  }
  return fileInfoFromAttributeData(full_path, data);
}

Api::IoCallBoolResult InstanceImplWin32::createPath(absl::string_view path) {
  std::error_code ec;
  while (!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  bool result = std::filesystem::create_directories(std::string{path}, ec);
  return ec ? resultFailure(false, ec.value()) : resultSuccess(result);
}

FileImplWin32::FlagsAndMode FileImplWin32::translateFlag(FlagSet in) {
  DWORD access = 0;
  DWORD creation = OPEN_EXISTING;

  if (in.test(File::Operation::Create)) {
    creation = OPEN_ALWAYS;
  }

  if (in.test(File::Operation::Write)) {
    access = GENERIC_WRITE;
  }

  // Order of tests matter here. There reason for that
  // is that `FILE_APPEND_DATA` should not be used together
  // with `GENERIC_WRITE`. If both of them are used the file
  // is not opened in append mode.
  if (in.test(File::Operation::Append)) {
    access = FILE_APPEND_DATA;
  }

  if (in.test(File::Operation::Read)) {
    access |= GENERIC_READ;
  }

  return {access, creation};
}

FilePtr InstanceImplWin32::createFile(const FilePathAndType& file_info) {
  switch (file_info.file_type_) {
  case DestinationType::File:
    return std::make_unique<FileImplWin32>(file_info);
  case DestinationType::TmpFile:
    if (!file_info.path_.empty() &&
        (file_info.path_.back() == '/' || file_info.path_.back() == '\\')) {
      return std::make_unique<TmpFileImplWin32>(FilePathAndType{
          DestinationType::TmpFile, file_info.path_.substr(0, file_info.path_.size() - 1)});
    }
    return std::make_unique<TmpFileImplWin32>(file_info);
  case DestinationType::Stderr:
    return std::make_unique<StdStreamFileImplWin32<STD_ERROR_HANDLE>>();
  case DestinationType::Stdout:
    return std::make_unique<StdStreamFileImplWin32<STD_OUTPUT_HANDLE>>();
  }
  return nullptr; // for gcc
}

bool InstanceImplWin32::fileExists(const std::string& path) {
  const DWORD attributes = ::GetFileAttributes(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES;
}

bool InstanceImplWin32::directoryExists(const std::string& path) {
  const DWORD attributes = ::GetFileAttributes(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return attributes & FILE_ATTRIBUTE_DIRECTORY;
}

ssize_t InstanceImplWin32::fileSize(const std::string& path) {
  // In ssl_integration_test::SslKeyLogTest cases, a temporary file is created, and then when
  // we check whether some logs are printed into the file, CreateFileA will fail and report the
  // error:"The process cannot access the file because it is being used by another process". Add
  // FILE_SHARE_WRITE flag to avoid such issue.
  auto fd = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                        OPEN_EXISTING, 0, NULL);
  if (fd == INVALID_HANDLE) {
    return -1;
  }
  ssize_t result = 0;
  LARGE_INTEGER lFileSize;
  BOOL bGetSize = GetFileSizeEx(fd, &lFileSize);
  CloseHandle(fd);
  if (!bGetSize) {
    return -1;
  }
  result += lFileSize.QuadPart;
  return result;
}

absl::StatusOr<std::string> InstanceImplWin32::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid path: ", path));
  }

  // In integration tests (and potentially in production) we rename config files and this creates
  // sharing violation errors while reading the file from a different thread. This is why we need to
  // add `FILE_SHARE_DELETE` to the sharing mode.
  auto fd = CreateFileA(path.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0,
                        NULL);
  if (fd == INVALID_HANDLE) {
    auto last_error = ::GetLastError();
    if (last_error == ERROR_FILE_NOT_FOUND) {
      return absl::InvalidArgumentError(absl::StrCat("Invalid path: ", path));
    }
    return absl::InvalidArgumentError(absl::StrCat("unable to read file: ", path));
  }
  DWORD buffer_size = 100;
  DWORD bytes_read = 0;
  std::vector<uint8_t> complete_buffer;
  do {
    std::vector<uint8_t> buffer(buffer_size);
    if (!ReadFile(fd, buffer.data(), buffer_size, &bytes_read, NULL)) {
      auto last_error = ::GetLastError();
      if (last_error == ERROR_FILE_NOT_FOUND) {
        CloseHandle(fd);
        return absl::InvalidArgumentError(absl::StrCat("Invalid path: ", path));
      }
      CloseHandle(fd);
      return absl::InvalidArgumentError(absl::StrCat("unable to read file: ", path));
    }
    complete_buffer.insert(complete_buffer.end(), buffer.begin(), buffer.begin() + bytes_read);
  } while (bytes_read == buffer_size);
  CloseHandle(fd);
  return std::string(complete_buffer.begin(), complete_buffer.end());
}

absl::StatusOr<PathSplitResult> InstanceImplWin32::splitPathFromFilename(absl::string_view path) {
  size_t last_slash = path.find_last_of(":/\\");
  if (last_slash == std::string::npos) {
    return absl::InvalidArgumentError(fmt::format("invalid file path {}", path));
  }
  absl::string_view name = path.substr(last_slash + 1);
  // Truncate all trailing slashes, but retain the entire
  // single '/', 'd:' drive, and 'd:\' drive root paths
  if (last_slash == 0 || path[last_slash] == ':' || path[last_slash - 1] == ':') {
    ++last_slash;
  }
  return PathSplitResult{path.substr(0, last_slash), name};
}

// clang-format off
//
// Filename warnings and caveats are documented at;
// https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
// Originally prepared by wrowe@rowe-clan.net for the Apache APR project, see;
// http://svn.apache.org/viewvc/apr/apr/trunk/file_io/win32/filesys.c?view=log&pathrev=62242
//
// Note special delimiter cases for path prefixes;
//     "D:\" for local drive volumes
//     "\server\share\" for network volumes
//     "\\?\" to pass path directly to the underlying driver
//          (invalidates the '/' separator and bypasses ".", ".." handling)
//     "\\?\D:\" for local drive volumes
//     "\\?\UNC\server\share\" for network volumes (literal "UNC")
//     "\\.\" for device namespace (e.g. volume names, character devices)
// File path components must not end in whitespace or '.' (except literal "." and "..")
// Allow ':' for drive letter only (attempt to name alternate file stream)
// Allow '/', '\\' as path delimiters only
// Valid file name character excluding delimiters;

static const char filename_char_table[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 //    !  "  #  $  %  &  '  (  )  *  +  ,  -  .  /  0  1  2  3  4  5  6  7  8  9  :  ;  <  =  >  ?
    1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0,
 // @  A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  P  Q  R  S  T  U  V  W  X  Y  Z  [  \  ]  ^  _
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
 // `  a  b  c  d  e  f  g  h  i  j  k  l  m  n  o  p  q  r  s  t  u  v  w  x  y  z  {  |  }  ~
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0,
 // High bit codes are accepted (subject to code page translation rules)
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

// The "COM#" and "LPT#" names below have boolean flag requiring a [1-9] suffix.
// This list can be avoided by observing dwFileAttributes & FILE_ATTRIBUTE_DEVICE
// within WIN32_FILE_ATTRIBUTE_DATA or WIN32_FIND_DATA results.
absl::node_hash_map<std::string, bool> pathelt_table = {
    {"CON", false}, {"NUL", false}, {"AUX", false}, {"PRN", false}, {"COM", true}, {"LPT", true}
};

// clang-format on

bool InstanceImplWin32::illegalPath(const std::string& path) {
  std::string pathbuffer = path;
  absl::string_view pathname = pathbuffer;

  // Examine and skip common leading path patterns of \\?\ and
  // reject paths with any other leading \\.\ device or an
  // unrecognized \\*\ prefix
  if ((pathname.size() >= 4) && (pathname[0] == '/' || pathname[0] == '\\') &&
      (pathname[1] == '/' || pathname[1] == '\\') && (pathname[3] == '/' || pathname[3] == '\\')) {
    if (pathname[2] == '?') {
      pathname = pathname.substr(4);
    } else {
      return true;
    }
  }
  // Examine and accept D: drive prefix (last opportunity to
  // accept a colon in the file path) and skip the D: component
  // This may result in a relative-to working directory or absolute path on D:
  if (pathname.size() >= 2 && std::isalpha(pathname[0]) && pathname[1] == ':') {
    pathname = pathname.substr(2);
  }
  std::string ucase_prefix("   ");
  std::vector<std::string> pathelts = absl::StrSplit(pathname, absl::ByAnyChar("/\\"));
  for (const std::string& elt : pathelts) {
    // Accept element of empty, ".", ".." as special cases,
    if (elt.size() == 0 ||
        (elt[0] == '.' && (elt.size() == 1 || (elt[1] == '.' && (elt.size() == 2))))) {
      continue;
    }
    // Upper-case path segment prefix to compare to character device names
    if (elt.size() >= 3) {
      int i;
      for (i = 0; i < 3; ++i) {
        ucase_prefix[i] = ::toupper(elt[i]);
      }
      auto found_elt = pathelt_table.find(ucase_prefix);

      if (found_elt != pathelt_table.end()) {
        // If a non-zero digit is significant, but not present, treat as not-found
        if (!found_elt->second || (elt.size() >= 4 && ::isdigit(elt[i]) && elt[i++] != '0')) {
          if (elt.size() == i) {
            return true;
          }
          // The literal device name is invalid for both an exact match,
          // and also when followed by (whitespace plus) any .ext suffix
          for (auto ch = elt.begin() + i; ch != elt.end(); ++ch) {
            if (*ch == '.') {
              return true;
            }
            if (*ch != ' ') {
              break;
            }
          }
        }
      }
    }

    for (const char& ch : elt) {
      if (!(filename_char_table[ch] & 1)) {
        return true;
      }
    }
    const char& lastch = elt[elt.size() - 1];
    if (lastch == ' ' || lastch == '.') {
      return true;
    }
  }

  return false;
}

} // namespace Filesystem
} // namespace Envoy
