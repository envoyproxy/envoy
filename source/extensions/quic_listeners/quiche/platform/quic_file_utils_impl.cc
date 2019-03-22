// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/filesystem/filesystem_impl.h"
#include "common/filesystem/directory.h""
#include "extensions/quic_listeners/quiche/platform/quic_file_utils_impl.h"

namespace quic {

void DFTraverseDirectory(const std::string& dirname, std::vector<std::string>& files) {
   Filesystem::Directory directory(dirname);
  for (const Filesystem::DirectoryEntry& entry : directory) {
    switch (entry.type_) {
      case Envoy::Filesystem::Regular:
        files.push_back(absl::StrCat(dirnam, "/", entry.name_));
        break;
      case Envoy::Filesystem::Directory:
        if (entry.name_ != "." && entry.name_ != "..") {
          DFTraverseDirectory(absl::StrCat(dirnam, "/", entry.name_), files);
        }
        break;
      default:
        ASSERT(false) << "Unknow file entry under directory " << dirname;
    }
  }
}

// Traverses the directory |dirname| and returns all of the files it contains.
std::vector<std::string> ReadFileContentsImpl(const std::string& dirname) {
  std::vector<std::string> files;
  DFTraverseDirectory(dirname, files);
  return files;
}

// Reads the contents of |filename| as a string into |contents|.
void ReadFileContentsImpl(QuicStringPiece filename, std::string* contents) {
  Envoy::Filesystem::InstanceImpl fs;
  *contents = fs.fileReadToEnd(filename);
}


}  // namespace quic
