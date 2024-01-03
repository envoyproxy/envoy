#include <filesystem>
#include <fstream>
#include <stack>
#include <string>

#include "source/common/filesystem/directory.h"

#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/container/node_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

using StatusHelpers::HasStatus;
using testing::HasSubstr;

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const DirectoryEntry& entry, std::ostream* os) {
  *os << "{name=" << entry.name_ << ", type=" << static_cast<int>(entry.type_) << ", size=";
  if (entry.size_bytes_ == absl::nullopt) {
    *os << "nullopt";
  } else {
    *os << entry.size_bytes_.value();
  }
  *os << "}";
}

class DirectoryTest : public testing::Test {
public:
  DirectoryTest() : dir_path_(TestEnvironment::temporaryPath("envoy_test")) {
    files_to_remove_.push(dir_path_);
  }

protected:
  void SetUp() override { TestEnvironment::createPath(dir_path_); }

  void TearDown() override {
    while (!files_to_remove_.empty()) {
      const std::string& f = files_to_remove_.top();
      TestEnvironment::removePath(f);
      files_to_remove_.pop();
    }
  }

  void addSubDirs(std::list<std::string> sub_dirs) {
    for (const std::string& dir_name : sub_dirs) {
      const std::string full_path = dir_path_ + "/" + dir_name;
      TestEnvironment::createPath(full_path);
      files_to_remove_.push(full_path);
    }
  }

  void addFiles(std::list<std::string> files) {
    for (const std::string& file_name : files) {
      const std::string full_path = dir_path_ + "/" + file_name;
      {
        const std::ofstream file(full_path);
        EXPECT_TRUE(file) << "failed to open test file";
      }
      files_to_remove_.push(full_path);
    }
  }

  void addFileWithContents(absl::string_view file_name, absl::string_view contents) {
    const std::string full_path = absl::StrCat(dir_path_, "/", file_name);
    {
      std::ofstream file(full_path);
      EXPECT_TRUE(file) << "failed to open test file";
      file << contents;
      file.close();
      EXPECT_TRUE(file) << "failed to write to test file";
    }
    files_to_remove_.push(full_path);
  }

  void addSymlinks(std::list<std::pair<std::string, std::string>> symlinks) {
    for (const auto& link : symlinks) {
      const std::string target_path = dir_path_ + "/" + link.first;
      const std::string link_path = dir_path_ + "/" + link.second;
      TestEnvironment::createSymlink(target_path, link_path);
      files_to_remove_.push(link_path);
    }
  }

  const std::string dir_path_;
  std::stack<std::string> files_to_remove_;
};

struct EntryHash {
  std::size_t operator()(DirectoryEntry const& e) const noexcept {
    return std::hash<std::string>{}(e.name_);
  }
};

using EntrySet = absl::node_hash_set<DirectoryEntry, EntryHash>;

EntrySet getDirectoryContents(const std::string& dir_path, bool recursive) {
  Directory directory(dir_path);
  EntrySet ret;
  for (const DirectoryEntry& entry : directory) {
    ret.insert(entry);
    if (entry.type_ == FileType::Directory && entry.name_ != "." && entry.name_ != ".." &&
        recursive) {
      std::string subdir_name = entry.name_;
      EntrySet subdir = getDirectoryContents(dir_path + "/" + subdir_name, recursive);
      for (const DirectoryEntry& entry : subdir) {
        ret.insert({subdir_name + "/" + entry.name_, entry.type_, entry.size_bytes_});
      }
    }
  }
  return ret;
}

// Test that we can list a file in a directory
TEST_F(DirectoryTest, DirectoryWithOneFile) {
  addFiles({"file"});
  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"file", FileType::Regular, 0},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

TEST_F(DirectoryTest, DirectoryWithOneFileIncludesCorrectFileSize) {
  absl::string_view contents = "hello!";
  addFileWithContents("file", contents);

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"file", FileType::Regular, contents.size()},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that we can list a sub directory in a directory
TEST_F(DirectoryTest, DirectoryWithOneDirectory) {
  addSubDirs({"sub_dir"});

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"sub_dir", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that we do not recurse into directories when listing files
TEST_F(DirectoryTest, DirectoryWithFileInSubDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"sub_dir/sub_file"});

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"sub_dir", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that when recursively creating DirectoryIterators, they do not interfere with each other
TEST_F(DirectoryTest, RecursionIntoSubDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"file", "sub_dir/sub_file"});

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"file", FileType::Regular, 0},
      {"sub_dir", FileType::Directory, absl::nullopt},
      {"sub_dir/sub_file", FileType::Regular, 0},
      {"sub_dir/.", FileType::Directory, absl::nullopt},
      {"sub_dir/..", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, true));
}

// Test that we can list a file and a sub directory in a directory
TEST_F(DirectoryTest, DirectoryWithFileAndDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"file"});

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"sub_dir", FileType::Directory, absl::nullopt},
      {"file", FileType::Regular, 0},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that a symlink to a file has type FileType::Regular
TEST_F(DirectoryTest, DirectoryWithSymlinkToFile) {
  const absl::string_view contents = "hello";
  addFileWithContents("file", contents);
  addSymlinks({{"file", "link"}});

  const EntrySet result = getDirectoryContents(dir_path_, false);
  EXPECT_THAT(result,
              testing::Contains(DirectoryEntry{"file", FileType::Regular, contents.size()}));
  // Validate without size for link, as it may be nullopt or file-size depending on OS.
  EXPECT_THAT(result, testing::Contains(testing::AllOf(
                          testing::Field(&DirectoryEntry::name_, "link"),
                          testing::Field(&DirectoryEntry::type_, FileType::Regular))));
}

// Test that a symlink to a directory has type FileType::Directory
TEST_F(DirectoryTest, DirectoryWithSymlinkToDirectory) {
  addSubDirs({"sub_dir"});
  addSymlinks({{"sub_dir", "link_dir"}});

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"sub_dir", FileType::Directory, absl::nullopt},
      {"link_dir", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that a broken symlink can be listed
TEST_F(DirectoryTest, DirectoryWithBrokenSymlink) {
  addSubDirs({"sub_dir"});
  addSymlinks({{"sub_dir", "link_dir"}});
  TestEnvironment::removePath(dir_path_ + "/sub_dir");

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
#ifndef WIN32
      // On Linux, a broken directory link is simply a symlink to be rm'ed
      {"link_dir", FileType::Regular, absl::nullopt},
#else
      // On Windows, a broken directory link remains a directory link to be rmdir'ed
      {"link_dir", FileType::Directory, absl::nullopt},
#endif
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

#ifndef WIN32
// Test that removing a file while iterating continues to the next file.
TEST_F(DirectoryTest, FileDeletedWhileIterating) {
  addFiles({"file", "file2"});
  DirectoryIteratorImpl dir_iterator(dir_path_);
  EntrySet found;
  TestEnvironment::removePath(dir_path_ + "/file");
  TestEnvironment::removePath(dir_path_ + "/file2");
  while (!(*dir_iterator).name_.empty()) {
    found.insert(*dir_iterator);
    ++dir_iterator;
  }
  // Test environment can potentially list files in any order, so if the
  // first file was one of the temporary files it will still be in the
  // set. The important thing is that at least one of them is *not* in
  // the set, and we didn't crash.
  EXPECT_THAT(found, testing::AnyOf(
                         EntrySet{
                             {".", FileType::Directory, absl::nullopt},
                             {"..", FileType::Directory, absl::nullopt},
                         },
                         EntrySet{
                             {".", FileType::Directory, absl::nullopt},
                             {"..", FileType::Directory, absl::nullopt},
                             {"file", FileType::Regular, 0},
                         },
                         EntrySet{
                             {".", FileType::Directory, absl::nullopt},
                             {"..", FileType::Directory, absl::nullopt},
                             {"file1", FileType::Regular, 0},
                         }));
}
#endif

// Test that we can list an empty directory
TEST_F(DirectoryTest, DirectoryWithEmptyDirectory) {
  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that the status is an error when given a non-existing path
TEST(DirectoryIteratorImpl, NonExistingDir) {
  const std::string dir_path("some/non/existing/dir");
  DirectoryIteratorImpl dir_iterator(dir_path);
#ifdef WIN32
  EXPECT_THAT(dir_iterator.status(),
              HasStatus(absl::StatusCode::kUnknown, HasSubstr("unable to open directory")));
#else
  EXPECT_THAT(dir_iterator.status(),
              HasStatus(absl::StatusCode::kNotFound, HasSubstr("unable to open directory")));
#endif
}

#ifndef WIN32
TEST_F(DirectoryTest, Fifo) {
  std::string fifo_path = fmt::format("{}/fifo", dir_path_);
  ASSERT_EQ(0, mkfifo(fifo_path.c_str(), 0644));

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
      {"fifo", FileType::Other, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
  remove(fifo_path.c_str());
}

// This test seems like it should be doable by removing a file after directory
// iteration begins, but apparently the behavior of that varies per-filesystem
// (in some cases the absent file is not seen, in others it is). So we test
// instead by directly calling the private function.
TEST_F(DirectoryTest, MakeEntryThrowsOnStatFailure) {
  Directory directory(dir_path_);
  EXPECT_THAT(directory.begin().makeEntry("foo"),
              HasStatus(absl::StatusCode::kNotFound, HasSubstr("unable to stat file")));
}
#endif

// Test that we correctly handle trailing path separators
TEST(Directory, DirectoryHasTrailingPathSeparator) {
#ifdef WIN32
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "\\");
#else
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "/");
#endif
  TestEnvironment::createPath(dir_path);

  const EntrySet expected = {
      {".", FileType::Directory, absl::nullopt},
      {"..", FileType::Directory, absl::nullopt},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
  TestEnvironment::removePath(dir_path);
}

TEST(DirectoryEntry, EqualityOperator) {
  std::vector<DirectoryEntry> values{
      DirectoryEntry{"bob", FileType::Directory, absl::nullopt},
      DirectoryEntry{"bob", FileType::Regular, absl::nullopt},
      DirectoryEntry{"bob", FileType::Other, absl::nullopt},
      DirectoryEntry{"bob", FileType::Regular, 0},
      DirectoryEntry{"bob", FileType::Regular, 5},
      DirectoryEntry{"bob", FileType::Regular, 6},
      DirectoryEntry{"alice", FileType::Regular, 6},
      DirectoryEntry{"jim", FileType::Regular, absl::nullopt},
  };
  for (size_t i = 0; i < values.size(); i++) {
    DirectoryEntry a = values[i];
    // Two copies of the same value should be ==, in either order.
    EXPECT_THAT(a, testing::Eq(values[i]));
    EXPECT_THAT(values[i], testing::Eq(a));
    for (size_t j = i + 1; j < values.size(); j++) {
      DirectoryEntry b = values[j];
      // No two pairs of the above DirectoryEntries should be ==, in either order.
      EXPECT_THAT(a, testing::Not(testing::Eq(b)));
      EXPECT_THAT(b, testing::Not(testing::Eq(a)));
    }
  }
}

} // namespace Filesystem
} // namespace Envoy
