#pragma once

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
  static DirectoryEntry makeEntry(const WIN32_FIND_DATA& find_data);

  HANDLE find_handle_;
};

} // namespace Filesystem
} // namespace Envoy
