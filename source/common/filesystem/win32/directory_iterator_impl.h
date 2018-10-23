#pragma once

#include <windows.h>

// <windows.h>, uses macros to #define a ton of symbols, two of which (DELETE and GetMessage)
// interfere with our code. DELETE shows up in the base.pb.h header generated from
// api/envoy/api/core/base.proto. Since it's a generated header, we can't #undef DELETE at
// the top of that header to avoid the collision. Similarly, GetMessage shows up in generated
// protobuf code so we can't #undef the symbol there.
#undef DELETE
#undef GetMessage

#include "envoy/filesystem/filesystem.h"

namespace Envoy {
namespace Filesystem {

class DirectoryIteratorImpl : public DirectoryIterator {
public:
  DirectoryIteratorImpl(const std::string& directory_path)
      : DirectoryIterator(directory_path), find_handle_(INVALID_HANDLE_VALUE) {}
  ~DirectoryIteratorImpl();

  DirectoryEntry nextEntry() override;

private:
  DirectoryEntry firstEntry();
  FileType fileType(const WIN32_FIND_DATA& find_data) const;

  HANDLE find_handle_;
};

} // namespace Filesystem
} // namespace Envoy