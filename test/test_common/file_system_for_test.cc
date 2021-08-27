#include "test/test_common/file_system_for_test.h"

#include "source/common/filesystem/filesystem_impl.h"

namespace Envoy {

namespace Filesystem {

MemfileInstanceImpl::MemfileInstanceImpl()
    : file_system_{new InstanceImpl()}, use_memfiles_(false) {}

MemfileInstanceImpl& fileSystemForTest() {
  static MemfileInstanceImpl* file_system = new MemfileInstanceImpl();
  return *file_system;
}

void MemfileInstanceImpl::renameFile(const std::string& old_name, const std::string& new_name) {
  {
    absl::MutexLock m(&lock_);
    // It's easy enough to change the key to the hash set, but most instances of
    // renameFile are to trigger file watches in core code, and those are not
    // mem-file-aware.
    RELEASE_ASSERT(!use_memfiles_,
                   "moving files not supported with memfile. Please call setUseMemfiles(false)");
  }
#ifdef WIN32
  // use MoveFileEx, since ::rename will not overwrite an existing file. See
  // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename?view=vs-2017
  // Note MoveFileEx cannot overwrite a directory as documented, nor a symlink, apparently.
  const BOOL rc = ::MoveFileEx(old_name.c_str(), new_name.c_str(), MOVEFILE_REPLACE_EXISTING);
  RELEASE_ASSERT(rc != 0, fmt::format("failed to rename file from  {} to {} with error {}",
                                      old_name, new_name, ::GetLastError()));
#else
  const int rc = ::rename(old_name.c_str(), new_name.c_str());
  RELEASE_ASSERT(rc == 0, "failed to rename file");
#endif
}

} // namespace Filesystem

} // namespace Envoy
