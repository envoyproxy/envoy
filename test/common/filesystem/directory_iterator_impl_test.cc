#include <fstream>
#include <string>
#include <unordered_set>

#include "common/filesystem/directory_iterator_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

// we are using this class to clean up all the files we create,
// as it looks like some versions of libstdc++ have a bug in
// std::experimental::filesystem::remove_all where it fails with nested directories:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71313

class ScopedPathRemover {
public:
  ScopedPathRemover(std::list<std::string> paths) : paths_(paths) {}

  ~ScopedPathRemover() {
    for (const std::string& path : paths_) {
      TestEnvironment::removePath(path);
    }
  }

private:
  std::list<std::string> paths_;
};

bool operator==(const DirectoryIterator::DirectoryEntry& lhs,
                const DirectoryIterator::DirectoryEntry& rhs) {
  return lhs.path_ == rhs.path_ && lhs.type_ == rhs.type_;
}

struct DirectoryEntryHash {
  std::size_t operator()(DirectoryIterator::DirectoryEntry const& e) const noexcept {
    return std::hash<std::string>{}(e.path_);
  }
};

typedef std::unordered_set<DirectoryIterator::DirectoryEntry, DirectoryEntryHash> EntrySet;

void checkDirectoryContents(const EntrySet expected, const std::string& dir_path) {
  DirectoryIteratorImpl dir_iterator(dir_path);
  EntrySet found;

  for (DirectoryIterator::DirectoryEntry entry = dir_iterator.nextEntry(); entry.path_ != "";
       entry = dir_iterator.nextEntry()) {
    found.insert(entry);
  }

  ASSERT_EQ(expected, found);
}

TEST(DirectoryIteratorImpl, DirectoryWithOneFile) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  ScopedPathRemover s({file_path, dir_path});

  TestUtility::createDirectory(dir_path);
  { std::ofstream file(file_path); }

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"file", DirectoryIterator::FileType::Regular},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithOneDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"sub_dir", DirectoryIterator::FileType::Directory},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithFileInSubDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  const std::string child_file_path(child_dir_path + "/" + "file");
  ScopedPathRemover s({child_file_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  { std::ofstream file(child_file_path); }

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"sub_dir", DirectoryIterator::FileType::Directory},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithFileAndDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({file_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  { std::ofstream file(file_path); }

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"sub_dir", DirectoryIterator::FileType::Directory},
      {"file", DirectoryIterator::FileType::Regular},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithSymlinkToFile) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  const std::string link_path(dir_path + "/" + "link");
  ScopedPathRemover s({link_path, file_path, dir_path});
  TestUtility::createDirectory(dir_path);
  { std::ofstream file(file_path); }
  TestUtility::createSymlink(file_path, link_path);

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"file", DirectoryIterator::FileType::Regular},
      {"link", DirectoryIterator::FileType::Regular},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithSymlinkToDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string link_dir_path(dir_path + "/" + "link_dir");
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({link_dir_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  TestUtility::createSymlink(child_dir_path, link_dir_path);

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
      {"sub_dir", DirectoryIterator::FileType::Directory},
      {"link_dir", DirectoryIterator::FileType::Directory},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, DirectoryWithEmptyDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  ScopedPathRemover s({dir_path});
  TestUtility::createDirectory(dir_path);

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
  };
  checkDirectoryContents(expected, dir_path);
}

TEST(DirectoryIteratorImpl, ContinueLookingInDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  ScopedPathRemover s({dir_path});
  TestUtility::createDirectory(dir_path);

  DirectoryIteratorImpl dir_iterator(dir_path);
  while (dir_iterator.nextEntry().path_ != "") {
  }
  // check that we can keep calling nextEntry() without an issue
  DirectoryIterator::DirectoryEntry empty = {"", DirectoryIterator::FileType::Other};
  ASSERT_EQ(dir_iterator.nextEntry(), empty);
  ASSERT_EQ(dir_iterator.nextEntry(), empty);
  ASSERT_EQ(dir_iterator.nextEntry(), empty);
}

TEST(DirectoryIteratorImpl, NonExistingDir) {
  const std::string dir_path(TestEnvironment::temporaryPath("some/non/existing/dir"));
  DirectoryIteratorImpl dir_iterator(dir_path);

#if !defined(WIN32)
  EXPECT_THROW_WITH_MESSAGE(
      dir_iterator.nextEntry(), EnvoyException,
      fmt::format("unable to open directory {}: No such file or directory", dir_path));
#else
  EXPECT_THROW_WITH_MESSAGE(
      dir_iterator.nextEntry(), EnvoyException,
      fmt::format("unable to open directory {}: {}", dir_path, ERROR_PATH_NOT_FOUND));
#endif
}

TEST(DirectoryIteratorImpl, DirectoryHasTrailingPathSeparator) {
#if !defined(WIN32)
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "/");
#else
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "\\");
#endif
  ScopedPathRemover s({dir_path});
  TestUtility::createDirectory(dir_path);

  EntrySet expected = {
      {".", DirectoryIterator::FileType::Directory},
      {"..", DirectoryIterator::FileType::Directory},
  };
  checkDirectoryContents(expected, dir_path);
}

} // namespace Filesystem
} // namespace Envoy