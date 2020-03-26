#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/filesystem/filesystem_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Filesystem {

FileImplWin32::~FileImplWin32() {
  if (isOpen()) {
    const Api::IoCallBoolResult result = close();
    ASSERT(result.rc_);
  }
}

void FileImplWin32::openFile(FlagSet in) {
  const auto flags_and_mode = translateFlag(in);
  fd_ = ::open(path_.c_str(), flags_and_mode.flags_, flags_and_mode.pmode_);
}

ssize_t FileImplWin32::writeFile(absl::string_view buffer) {
  return ::_write(fd_, buffer.data(), buffer.size());
}

FileImplWin32::FlagsAndMode FileImplWin32::translateFlag(FlagSet in) {
  int out = 0;
  int pmode = 0;
  if (in.test(File::Operation::Create)) {
    out |= _O_CREAT;
    pmode |= _S_IREAD | _S_IWRITE;
  }

  if (in.test(File::Operation::Append)) {
    out |= _O_APPEND;
  }

  if (in.test(File::Operation::Read) && in.test(File::Operation::Write)) {
    out |= _O_RDWR;
  } else if (in.test(File::Operation::Read)) {
    out |= _O_RDONLY;
  } else if (in.test(File::Operation::Write)) {
    out |= _O_WRONLY;
  }

  return {out, pmode};
}

bool FileImplWin32::closeFile() { return ::_close(fd_) != -1; }

FilePtr InstanceImplWin32::createFile(const std::string& path) {
  return std::make_unique<FileImplWin32>(path);
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
  struct _stat info;
  if (::_stat(path.c_str(), &info) != 0) {
    return -1;
  }
  return info.st_size;
}

std::string InstanceImplWin32::fileReadToEnd(const std::string& path) {
  if (illegalPath(path)) {
    throw EnvoyException(absl::StrCat("Invalid path: ", path));
  }

  std::ios::sync_with_stdio(false);

  // On Windows, we need to explicitly set the file mode as binary. Otherwise,
  // 0x1a will be treated as EOF
  std::ifstream file(path, std::ios_base::binary);
  if (file.fail()) {
    throw EnvoyException(absl::StrCat("unable to read file: ", path));
  }

  std::stringstream file_string;
  file_string << file.rdbuf();

  return file_string.str();
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
std::unordered_map<std::string, bool> pathelt_table = {
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
