#pragma once

#include <windows.h>

// <windows.h> uses macros to #define a ton of symbols, two of which (DELETE and GetMessage)
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
  DirectoryIteratorImpl(const std::string& directory_path);
  DirectoryIteratorImpl() : DirectoryIterator(), find_handle_(INVALID_HANDLE_VALUE) {}
  ~DirectoryIteratorImpl();

  DirectoryIteratorImpl& operator++() override;

  // We don't want this iterator to be copied. If the copy gets destructed,
  // then it will close its copy of the directory HANDLE, which will cause the
  // original's to be invalid. While we could implement a deep copy constructor to
  // work around this, it is not needed the moment.
  DirectoryIteratorImpl(const DirectoryIteratorImpl&) = delete;
  DirectoryIteratorImpl(DirectoryIteratorImpl&&) = default;
  DirectoryIteratorImpl& operator=(DirectoryIteratorImpl&&) = default;

private:
  FileType fileType(const WIN32_FIND_DATA& find_data) const;

  HANDLE find_handle_;
};

} // namespace Filesystem
} // namespace Envoy
