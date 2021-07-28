#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
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
  fd_ = CreateFileA(path().c_str(), flags.access_, FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                    flags.creation_, 0, NULL);
  if (fd_ == INVALID_HANDLE) {
    return resultFailure(false, ::GetLastError());
  }
  return resultSuccess(true);
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
  case DestinationType::Stderr:
    return std::make_unique<StdStreamFileImplWin32<STD_ERROR_HANDLE>>();
  case DestinationType::Stdout:
    return std::make_unique<StdStreamFileImplWin32<STD_OUTPUT_HANDLE>>();
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
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
  auto fd = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, NULL);
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

std::string InstanceImplWin32::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    throw EnvoyException(absl::StrCat("Invalid path: ", path));
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
      throw EnvoyException(absl::StrCat("Invalid path: ", path));
    }
    throw EnvoyException(absl::StrCat("unable to read file: ", path));
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
        throw EnvoyException(absl::StrCat("Invalid path: ", path));
      }
      CloseHandle(fd);
      throw EnvoyException(absl::StrCat("unable to read file: ", path));
    }
    complete_buffer.insert(complete_buffer.end(), buffer.begin(), buffer.begin() + bytes_read);
  } while (bytes_read == buffer_size);
  CloseHandle(fd);
  return std::string(complete_buffer.begin(), complete_buffer.end());
}

PathSplitResult InstanceImplWin32::splitPathFromFilename(absl::string_view path) {
  size_t last_slash = path.find_last_of(":/\\");
  if (last_slash == std::string::npos) {
    throw EnvoyException(fmt::format("invalid file path {}", path));
  }
  absl::string_view name = path.substr(last_slash + 1);
  // Truncate all trailing slashes, but retain the entire
  // single '/', 'd:' drive, and 'd:\' drive root paths
  if (last_slash == 0 || path[last_slash] == ':' || path[last_slash - 1] == ':') {
    ++last_slash;
  }
  return {path.substr(0, last_slash), name};
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
