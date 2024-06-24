#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/directory.h"
#include "source/common/filesystem/filesystem_impl.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

static constexpr FlagSet DefaultFlags{
    1 << Filesystem::File::Operation::Read | 1 << Filesystem::File::Operation::Write |
    1 << Filesystem::File::Operation::Create | 1 << Filesystem::File::Operation::Append};

class FileSystemImplTest : public testing::Test {
protected:
  filesystem_os_id_t getFd(File* file) {
    auto file_impl = dynamic_cast<FileImpl*>(file);
    RELEASE_ASSERT(file_impl != nullptr, "failed to cast File* to FileImpl*");
    return file_impl->fd_;
  }
#ifndef WIN32
  std::string getTmpFileName(File* file) {
    auto impl = dynamic_cast<TmpFileImplPosix*>(file);
    RELEASE_ASSERT(impl != nullptr, "failed to cast File* to TmpFileImplPosix*");
    return impl->tmp_file_path_;
  }
  Api::SysCallStringResult canonicalPath(const std::string& path) {
    return file_system_.canonicalPath(path);
  }
  Api::IoCallBoolResult openNamedTmpFile(FilePtr& file, bool with_unlink = true) {
    TmpFileImplPosix* impl = dynamic_cast<TmpFileImplPosix*>(file.get());
    RELEASE_ASSERT(impl != nullptr, "failed to cast File* to TmpFileImplPosix*");
    return impl->openNamedTmpFile(impl->translateFlag(DefaultFlags), with_unlink);
  }
#endif
  InstanceImpl file_system_;
};

TEST_F(FileSystemImplTest, FileExists) {
  EXPECT_FALSE(file_system_.fileExists("/dev/blahblahblah"));
#ifdef WIN32
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", "x");
  EXPECT_TRUE(file_system_.fileExists(file_path));
  EXPECT_TRUE(file_system_.fileExists("c:/windows"));
#else
  EXPECT_TRUE(file_system_.fileExists("/dev/null"));
  EXPECT_TRUE(file_system_.fileExists("/dev"));
#endif
}

TEST_F(FileSystemImplTest, DirectoryExists) {
  EXPECT_FALSE(file_system_.directoryExists("/dev/blahblah"));
#ifdef WIN32
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", "x");
  EXPECT_FALSE(file_system_.directoryExists(file_path));
  EXPECT_TRUE(file_system_.directoryExists("c:/windows"));
#else
  EXPECT_FALSE(file_system_.directoryExists("/dev/null"));
  EXPECT_TRUE(file_system_.directoryExists("/dev"));
#endif
}

TEST_F(FileSystemImplTest, FileSize) {
  EXPECT_EQ(0, file_system_.fileSize(std::string(Platform::null_device_path)));
  EXPECT_EQ(-1, file_system_.fileSize("/dev/blahblahblah"));
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);
  EXPECT_EQ(data.length(), file_system_.fileSize(file_path));
}

TEST_F(FileSystemImplTest, FileReadToEndSuccess) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  EXPECT_EQ(data, file_system_.fileReadToEnd(file_path).value());
}

// Files are read into std::string; verify that all bytes (e.g., non-ascii characters) come back
// unmodified
TEST_F(FileSystemImplTest, FileReadToEndSuccessBinary) {
  std::string data;
  for (unsigned i = 0; i < 256; i++) {
    data.push_back(i);
  }
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  const std::string read = file_system_.fileReadToEnd(file_path).value();
  const std::vector<uint8_t> binary_read(read.begin(), read.end());
  EXPECT_EQ(binary_read.size(), 256);
  for (unsigned i = 0; i < 256; i++) {
    EXPECT_EQ(binary_read.at(i), i);
  }
}

TEST_F(FileSystemImplTest, FileReadToEndPathDoesNotExist) {
  unlink(TestEnvironment::temporaryPath("envoy_this_not_exist").c_str());
  EXPECT_THAT(file_system_.fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist"))
                  .status()
                  .message(),
              testing::StartsWith("Invalid path:"));
}

#ifndef WIN32
// In Windows this method of removing the permissions does not make read fail.
// Issue https://github.com/envoyproxy/envoy/issues/25614, disabling this test
TEST_F(FileSystemImplTest, DISABLED_FileReadToEndNotReadable) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  std::filesystem::permissions(file_path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::remove);
  EXPECT_FALSE(file_system_.fileReadToEnd(file_path).status().ok());
}
#endif

TEST_F(FileSystemImplTest, FileReadToEndDenylisted) {
  EXPECT_FALSE(file_system_.fileReadToEnd("/dev/urandom").status().ok());
  EXPECT_FALSE(file_system_.fileReadToEnd("/proc/cpuinfo").status().ok());
  EXPECT_FALSE(file_system_.fileReadToEnd("/sys/block/sda/dev").status().ok());
}

#ifndef WIN32
TEST_F(FileSystemImplTest, CanonicalPathSuccess) {
  EXPECT_EQ("/", canonicalPath("//").return_value_);
}
#endif

#ifndef WIN32
TEST_F(FileSystemImplTest, CanonicalPathFail) {
  const Api::SysCallStringResult result = canonicalPath("/_some_non_existent_file");
  EXPECT_TRUE(result.return_value_.empty());
  EXPECT_EQ("No such file or directory", errorDetails(result.errno_));
}
#endif

TEST_F(FileSystemImplTest, SplitPathFromFilename) {
  PathSplitResult result;
  result = file_system_.splitPathFromFilename("/foo/bar/baz").value();
  EXPECT_EQ(result.directory_, "/foo/bar");
  EXPECT_EQ(result.file_, "baz");
  result = file_system_.splitPathFromFilename("/foo/bar").value();
  EXPECT_EQ(result.directory_, "/foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("/foo").value();
  EXPECT_EQ(result.directory_, "/");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("/").value();
  EXPECT_EQ(result.directory_, "/");
  EXPECT_EQ(result.file_, "");
  EXPECT_FALSE(file_system_.splitPathFromFilename("nopathdelimeter").ok());
#ifdef WIN32
  result = file_system_.splitPathFromFilename("c:\\foo/bar").value();
  EXPECT_EQ(result.directory_, "c:\\foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("c:/foo\\bar").value();
  EXPECT_EQ(result.directory_, "c:/foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("c:\\foo").value();
  EXPECT_EQ(result.directory_, "c:\\");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("c:foo").value();
  EXPECT_EQ(result.directory_, "c:");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("c:").value();
  EXPECT_EQ(result.directory_, "c:");
  EXPECT_EQ(result.file_, "");
  result = file_system_.splitPathFromFilename("\\\\?\\C:\\").value();
  EXPECT_EQ(result.directory_, "\\\\?\\C:\\");
  EXPECT_EQ(result.file_, "");
#endif
}

TEST_F(FileSystemImplTest, IllegalPath) {
  EXPECT_FALSE(file_system_.illegalPath("/"));
  EXPECT_FALSE(file_system_.illegalPath("//"));
#ifdef WIN32
  EXPECT_FALSE(file_system_.illegalPath("/dev"));
  EXPECT_FALSE(file_system_.illegalPath("/dev/"));
  EXPECT_FALSE(file_system_.illegalPath("/proc"));
  EXPECT_FALSE(file_system_.illegalPath("/proc/"));
  EXPECT_FALSE(file_system_.illegalPath("/sys"));
  EXPECT_FALSE(file_system_.illegalPath("/sys/"));
  EXPECT_FALSE(file_system_.illegalPath("/_some_non_existent_file"));
  EXPECT_TRUE(file_system_.illegalPath(R"EOF(\\.\foo)EOF"));
  EXPECT_TRUE(file_system_.illegalPath(R"EOF(\\z\foo)EOF"));
  EXPECT_TRUE(file_system_.illegalPath(R"EOF(\\?\nul)EOF"));
  EXPECT_FALSE(file_system_.illegalPath(R"EOF(\\?\C:\foo)EOF"));
  EXPECT_FALSE(file_system_.illegalPath(R"EOF(C:\foo)EOF"));
  EXPECT_FALSE(file_system_.illegalPath(R"EOF(C:\foo/bar\baz)EOF"));
  EXPECT_FALSE(file_system_.illegalPath("C:/foo"));
  EXPECT_FALSE(file_system_.illegalPath("C:zfoo"));
  EXPECT_FALSE(file_system_.illegalPath("C:/foo/bar/baz"));
  EXPECT_TRUE(file_system_.illegalPath("C:/foo/b*ar/baz"));
  EXPECT_TRUE(file_system_.illegalPath("C:/foo/b?ar/baz"));
  EXPECT_TRUE(file_system_.illegalPath(R"EOF(C:/i/x"x)EOF"));
  EXPECT_TRUE(file_system_.illegalPath(std::string("C:/i\0j", 6)));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/\177"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/\alarm"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/../j"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/./j"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/.j"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/j."));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/j "));
  EXPECT_FALSE(file_system_.illegalPath("C:/i///"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/NUL"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/nul"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/nul.ext"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/nul.ext.ext2"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/nul .ext"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/COM1"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/COM1/whoops"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/COM1.ext"));
  EXPECT_TRUE(file_system_.illegalPath("C:/i/COM1  .ext"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/COM1  ext"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/COM1foo"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/COM0"));
  EXPECT_FALSE(file_system_.illegalPath("C:/i/COM"));
#else
  EXPECT_TRUE(file_system_.illegalPath("/dev"));
  EXPECT_TRUE(file_system_.illegalPath("/dev/"));
  // Exception to allow opening from file descriptors. See #7258.
  EXPECT_FALSE(file_system_.illegalPath("/dev/fd/0"));
  EXPECT_TRUE(file_system_.illegalPath("/proc"));
  EXPECT_TRUE(file_system_.illegalPath("/proc/"));
  EXPECT_TRUE(file_system_.illegalPath("/sys"));
  EXPECT_TRUE(file_system_.illegalPath("/sys/"));
  EXPECT_TRUE(file_system_.illegalPath("/_some_non_existent_file"));
#endif
}

TEST_F(FileSystemImplTest, ConstructedFile) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());
  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};

  FilePtr file = file_system_.createFile(new_file_info);
  EXPECT_EQ(file->path(), new_file_path);
  EXPECT_EQ(file->destinationType(), Filesystem::DestinationType::File);
}

TEST_F(FileSystemImplTest, ConstructedStdErrFile) {
  FilePathAndType new_file_info{Filesystem::DestinationType::Stderr, ""};

  FilePtr file = file_system_.createFile(new_file_info);
  EXPECT_FALSE(file->isOpen());
  EXPECT_EQ(file->destinationType(), Filesystem::DestinationType::Stderr);
  EXPECT_EQ(file->path(), "/dev/stderr");
}

TEST_F(FileSystemImplTest, ConstructedStdOutFile) {
  FilePathAndType new_file_info{Filesystem::DestinationType::Stdout, ""};

  FilePtr file = file_system_.createFile(new_file_info);
  EXPECT_FALSE(file->isOpen());
  EXPECT_EQ(file->destinationType(), Filesystem::DestinationType::Stdout);
  EXPECT_EQ(file->path(), "/dev/stdout");
}

TEST_F(FileSystemImplTest, ConstructedFileNotOpen) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(FileSystemImplTest, Open) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = file->open(DefaultFlags);
  EXPECT_TRUE(result.return_value_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenReadOnly) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());
  static constexpr FlagSet ReadOnlyFlags{1 << Filesystem::File::Operation::Read |
                                         1 << Filesystem::File::Operation::Create |
                                         1 << Filesystem::File::Operation::Append};

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = file->open(ReadOnlyFlags);
  EXPECT_TRUE(result.return_value_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, PreadReadsTheSpecifiedRange) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  char buf[5];
  absl::string_view view{buf, sizeof(buf)};
  FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
  FilePtr file = file_system_.createFile(file_info);
  const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
  EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
  const Api::IoCallSizeResult read_result = file->pread(buf, sizeof(buf), 2);
  EXPECT_EQ(read_result.return_value_, sizeof(buf)) << read_result.err_->getErrorDetails();
  EXPECT_THAT(read_result.err_, ::testing::IsNull());
  EXPECT_EQ(view, "23456");
}

TEST_F(FileSystemImplTest, PreadFailureReturnsError) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  char buf[5];
  absl::string_view view{buf, sizeof(buf)};
  FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
  FilePtr file = file_system_.createFile(file_info);
  // Open with write-only permission so that `pread` should fail.
  const Api::IoCallBoolResult open_result =
      file->open(FlagSet{1 << Filesystem::File::Operation::Write});
  EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
  const Api::IoCallSizeResult read_result = file->pread(buf, sizeof(buf), 2);
  EXPECT_EQ(read_result.return_value_, -1);
  EXPECT_THAT(read_result.err_, ::testing::Not(::testing::IsNull()));
}

TEST_F(FileSystemImplTest, PwriteWritesTheSpecifiedRange) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  {
    const char buf[6] = "BOOPS";
    absl::string_view view{buf};
    FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
    FilePtr file = file_system_.createFile(file_info);
    const Api::IoCallBoolResult open_result = file->open(FlagSet{
        (1 << Filesystem::File::Operation::Write) | (1 << Filesystem::File::Operation::Read) |
        (1 << Filesystem::File::Operation::KeepExistingData)});
    EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
    const Api::IoCallSizeResult write_result = file->pwrite(buf, view.size(), 2);
    EXPECT_EQ(write_result.return_value_, view.size()) << write_result.err_->getErrorDetails();
    EXPECT_THAT(write_result.err_, ::testing::IsNull());
    // buf shouldn't have changed.
    EXPECT_EQ(view, "BOOPS");
  }
  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ(contents, "01BOOPS789");
}

TEST_F(FileSystemImplTest, StatOnDirectoryReturnsDirectoryType) {
  const std::string new_dir_path = TestEnvironment::temporaryPath("envoy_test_dir");
  TestEnvironment::createPath(new_dir_path);
  Cleanup cleanup{[new_dir_path]() { TestEnvironment::removePath(new_dir_path); }};
  const Api::IoCallResult<FileInfo> info_result = file_system_.stat(new_dir_path);
  EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
  EXPECT_EQ(info_result.return_value_.name_, "envoy_test_dir");
  EXPECT_EQ(info_result.return_value_.file_type_, FileType::Directory);
}

TEST_F(FileSystemImplTest, CreatePathCreatesDirectoryAndReturnsSuccessForExistingDirectory) {
  const std::string new_dir_path = TestEnvironment::temporaryPath("envoy_test_dir");
  Api::IoCallBoolResult result = file_system_.createPath(new_dir_path);
  Cleanup cleanup{[new_dir_path]() { TestEnvironment::removePath(new_dir_path); }};
  EXPECT_TRUE(result.return_value_);
  EXPECT_THAT(result.err_, ::testing::IsNull()) << result.err_->getErrorDetails();
  // A bit awkwardly using file_system_.stat to test that the directory exists, because
  // otherwise we might have to do windows/linux conditional code for this.
  const Api::IoCallResult<FileInfo> info_result = file_system_.stat(new_dir_path);
  EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
  EXPECT_EQ(info_result.return_value_.name_, "envoy_test_dir");
  EXPECT_EQ(info_result.return_value_.file_type_, FileType::Directory);
  // Ensure it returns true if we 'create' an existing path too.
  result = file_system_.createPath(new_dir_path);
  EXPECT_FALSE(result.return_value_);
  EXPECT_THAT(result.err_, ::testing::IsNull()) << result.err_->getErrorDetails();
}

TEST_F(FileSystemImplTest, CreatePathCreatesDirectoryGivenTrailingSlash) {
  const std::string new_dir_path = TestEnvironment::temporaryPath("envoy_test_dir/");
  const Api::IoCallBoolResult result = file_system_.createPath(new_dir_path);
  Cleanup cleanup{[new_dir_path]() {
    TestEnvironment::removePath(new_dir_path.substr(0, new_dir_path.size() - 1));
  }};
  EXPECT_TRUE(result.return_value_);
  EXPECT_THAT(result.err_, ::testing::IsNull()) << result.err_->getErrorDetails();
  // A bit awkwardly using file_system_.stat to test that the directory exists, because
  // otherwise we might have to do windows/linux conditional code for this.
  const Api::IoCallResult<FileInfo> info_result =
      file_system_.stat(absl::string_view{new_dir_path}.substr(0, new_dir_path.size() - 1));
  EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
  EXPECT_EQ(info_result.return_value_.name_, "envoy_test_dir");
  EXPECT_EQ(info_result.return_value_.file_type_, FileType::Directory);
}

TEST_F(FileSystemImplTest, CreatePathReturnsErrorOnNoPermissionToWriteDir) {
#ifdef WIN32
  GTEST_SKIP() << "It's hard to not have permission to create a directory on Windows";
#endif
  const std::string new_dir_path = "/should_fail_to_create_directory_in_root/x/y";
  const Api::IoCallBoolResult result = file_system_.createPath(new_dir_path);
  EXPECT_FALSE(result.return_value_);
  EXPECT_THAT(result.err_, testing::Not(testing::IsNull()));
}

TEST_F(FileSystemImplTest, StatOnFileOpenOrClosedMeasuresTheExpectedValues) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  {
    const char buf[6] = "BOOPS";
    absl::string_view view{buf};
    FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
    FilePtr file = file_system_.createFile(file_info);
    const Api::IoCallBoolResult open_result = file->open(FlagSet{
        (1 << Filesystem::File::Operation::Write) | (1 << Filesystem::File::Operation::Read) |
        (1 << Filesystem::File::Operation::KeepExistingData)});
    EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
    const Api::IoCallSizeResult write_result = file->pwrite(buf, view.size(), 8);
    EXPECT_EQ(write_result.return_value_, view.size()) << write_result.err_->getErrorDetails();
    EXPECT_THAT(write_result.err_, ::testing::IsNull());
    {
      // Verify info() on an open file.
      const Api::IoCallResult<FileInfo> info_result = file->info();
      EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
      EXPECT_EQ(info_result.return_value_.size_, 13U);
      EXPECT_EQ(info_result.return_value_.name_, "test_envoy");
      EXPECT_EQ(info_result.return_value_.file_type_, FileType::Regular);
      // File system uses real system clock, so to validate that the value retrieved by info() is
      // reasonable, we must also use the real system clock here.
      SystemTime now = std::chrono::system_clock::now(); // NO_CHECK_FORMAT(real_time)
      // Verify that the file times on the created file are within the last 5 seconds.
      EXPECT_THAT(info_result.return_value_.time_created_,
                  testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
      EXPECT_THAT(info_result.return_value_.time_last_accessed_,
                  testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
      EXPECT_THAT(info_result.return_value_.time_last_modified_,
                  testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
    }
  }
  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ(contents, "01234567BOOPS");
  {
    // Verify stat() on a non-open file.
    const Api::IoCallResult<FileInfo> info_result = file_system_.stat(file_path);
    EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
    EXPECT_EQ(info_result.return_value_.size_, 13U);
    EXPECT_EQ(info_result.return_value_.name_, "test_envoy");
    EXPECT_EQ(info_result.return_value_.file_type_, FileType::Regular);
    // File system uses real system clock, so to validate that the value retrieved by stat() is
    // reasonable, we must also use the real system clock here.
    SystemTime now = std::chrono::system_clock::now(); // NO_CHECK_FORMAT(real_time)
    // Verify that the file times on the created file are within the last 5 seconds.
    EXPECT_THAT(info_result.return_value_.time_created_,
                testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
    EXPECT_THAT(info_result.return_value_.time_last_accessed_,
                testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
    EXPECT_THAT(info_result.return_value_.time_last_modified_,
                testing::AllOf(testing::Gt(now - std::chrono::seconds(5)), testing::Le(now)));
  }
}

#ifndef WIN32
// We can't make a broken symlink the same way in Windows.
TEST_F(FileSystemImplTest, StatOnBrokenSymlinkReturnsRegularFile) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  const std::string link_path = absl::StrCat(file_path, "_link");
  EXPECT_EQ(0, ::symlink(file_path.c_str(), link_path.c_str())) << errno;
  EXPECT_EQ(0, ::unlink(file_path.c_str())) << errno;
  const Api::IoCallResult<FileInfo> info_result = file_system_.stat(link_path);
  EXPECT_THAT(info_result.err_, ::testing::IsNull()) << info_result.err_->getErrorDetails();
  EXPECT_EQ(info_result.return_value_.file_type_, FileType::Regular);
  EXPECT_EQ(info_result.return_value_.name_, "test_envoy_link");
  EXPECT_EQ(0, ::unlink(link_path.c_str())) << errno;
}
#endif

#ifndef WIN32
// There's no `mkfifo` on Windows.
TEST_F(FileSystemImplTest, StatOnFifoReturnsOtherFileType) {
  const std::string fifo_path = TestEnvironment::temporaryPath("test_envoy_fifo");
  ::mkfifo(fifo_path.c_str(), 0666);
  const Api::IoCallResult<FileInfo> info_result = file_system_.stat(fifo_path);
  if (info_result.err_ != nullptr) {
    // Only do this test if we created a pipe successfully. If the test env can't
    // do it then we can't test this behavior.
    Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};
    EXPECT_EQ(info_result.return_value_.file_type_, FileType::Other);
    EXPECT_EQ(info_result.return_value_.name_, "test_envoy_fifo");
  }
}
#endif

#ifndef WIN32
// ::close doesn't work with WIN32
TEST_F(FileSystemImplTest, InfoOnInvalidedFileDescriptorReturnsError) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
  FilePtr file = file_system_.createFile(file_info);
  const Api::IoCallBoolResult open_result = file->open(
      FlagSet{(1 << Filesystem::File::Operation::Write) | (1 << Filesystem::File::Operation::Read) |
              (1 << Filesystem::File::Operation::KeepExistingData)});
  EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
  // Close the file descriptor to make it invalid.
  EXPECT_EQ(0, ::close(getFd(file.get())));
  const Api::IoCallResult<FileInfo> info_result = file->info();
  EXPECT_THAT(info_result.err_, testing::NotNull());
  // Close the file even though it's already closed, so we don't assert in the destructor.
  file->close();
}
#endif

TEST_F(FileSystemImplTest, StatOnNonexistentFileReturnsError) {
  const std::string nonexistent_path =
      TestEnvironment::temporaryPath("test_envoy_nonexistent_file");
  const Api::IoCallResult<FileInfo> stat_result = file_system_.stat(nonexistent_path);
  EXPECT_THAT(stat_result.err_, testing::NotNull());
}

TEST_F(FileSystemImplTest, PwriteFailureReturnsError) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "0123456789");
  const char buf[6] = "BOOPS";
  absl::string_view view{buf};
  FilePathAndType file_info{Filesystem::DestinationType::File, file_path};
  FilePtr file = file_system_.createFile(file_info);
  // Open with read-only permission so write should fail.
  const Api::IoCallBoolResult open_result =
      file->open(FlagSet{1 << Filesystem::File::Operation::Read});
  EXPECT_TRUE(open_result.return_value_) << open_result.err_->getErrorDetails();
  const Api::IoCallSizeResult write_result = file->pwrite(buf, view.size(), 2);
  EXPECT_EQ(write_result.return_value_, -1);
  EXPECT_THAT(write_result.err_, ::testing::Not(::testing::IsNull()));
}

TEST_F(FileSystemImplTest, TemporaryFileIsDeletedOnClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = file->open(DefaultFlags);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  EXPECT_TRUE(file->isOpen());
  file.reset();
  std::vector<std::string> found_files;
  for (const DirectoryEntry& entry : Directory(new_file_path)) {
    found_files.push_back(entry.name_);
  }
  // After the tmp file is closed, there should be no file persisting in the directory.
  // (We don't necessarily expect that a named file was created - that depends on the
  // platform and filesystem - but after the file is closed any named file that may
  // have been created should have been destroyed.)
  EXPECT_THAT(found_files, testing::Not(testing::Contains(testing::EndsWith(".tmp"))));
}

TEST_F(FileSystemImplTest, TemporaryFileOpenedTwiceIsDeletedOnClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = file->open(DefaultFlags);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  EXPECT_TRUE(file->isOpen());
  const filesystem_os_id_t initial_fd = getFd(file.get());
  const Api::IoCallBoolResult result2 = file->open(DefaultFlags);
  EXPECT_TRUE(result2.return_value_) << result2.err_->getErrorDetails();
  EXPECT_TRUE(file->isOpen());
  EXPECT_EQ(initial_fd, getFd(file.get()));
  file.reset();
  std::vector<std::string> found_files;
  for (const DirectoryEntry& entry : Directory(new_file_path)) {
    found_files.push_back(entry.name_);
  }
  // After the tmp file is closed, there should be no file persisting in the directory.
  // (We don't necessarily expect that a named file was created - that depends on the
  // platform and filesystem - but after the file is closed any named file that may
  // have been created should have been destroyed.)
  EXPECT_THAT(found_files, testing::Not(testing::Contains(testing::EndsWith(".tmp"))));
}

#ifndef WIN32
TEST_F(FileSystemImplTest, NamedTemporaryFileDeletedOnClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = openNamedTmpFile(file);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  file.reset();
  std::vector<std::string> found_files;
  for (const DirectoryEntry& entry : Directory(new_file_path)) {
    found_files.push_back(entry.name_);
  }
  // After the tmp file is closed, there should be no file persisting in the directory.
  // (We don't necessarily expect that a named file was created - that depends on the
  // platform and filesystem - but after the file is closed any named file that may
  // have been created should have been destroyed.)
  EXPECT_THAT(found_files, testing::Not(testing::Contains(testing::EndsWith(".tmp"))));
}

TEST_F(FileSystemImplTest, NamedTemporaryFileCloseUnlinkFailureReturnsError) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = openNamedTmpFile(file, false);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  EXPECT_EQ(0, ::unlink(getTmpFileName(file.get()).c_str()));
  const Api::IoCallBoolResult close_result = file->close();
  EXPECT_FALSE(close_result.return_value_)
      << "file close unexpectedly returned success on already unlinked file";
}

TEST_F(FileSystemImplTest, NamedTemporaryFileCloseFailureReturnsError) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = openNamedTmpFile(file, false);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  EXPECT_EQ(0, ::close(getFd(file.get())));
  const Api::IoCallBoolResult close_result = file->close();
  EXPECT_FALSE(close_result.return_value_)
      << "file close unexpectedly returned success on already closed file";
}

TEST_F(FileSystemImplTest, NamedTemporaryFileThatCouldntUnlinkOnOpenDeletedOnClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = openNamedTmpFile(file, false);
  EXPECT_TRUE(result.return_value_) << result.err_->getErrorDetails();
  file.reset();
  std::vector<std::string> found_files;
  for (const DirectoryEntry& entry : Directory(new_file_path)) {
    found_files.push_back(entry.name_);
  }
  // After the tmp file is closed, there should be no file persisting in the directory.
  // (We don't necessarily expect that a named file was created - that depends on the
  // platform and filesystem - but after the file is closed any named file that may
  // have been created should have been destroyed.)
  EXPECT_THAT(found_files, testing::Not(testing::Contains(testing::EndsWith(".tmp"))));
}

TEST_F(FileSystemImplTest, NamedTemporaryFileFailureToOpenReturnsError) {
  const std::string new_file_path = TestEnvironment::temporaryPath("nonexistent_path");
  FilePathAndType new_file_info{Filesystem::DestinationType::TmpFile, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = openNamedTmpFile(file);
  EXPECT_FALSE(result.return_value_);
  EXPECT_TRUE(result.err_);
}
#endif

TEST_F(FileSystemImplTest, OpenTwice) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  EXPECT_EQ(getFd(file.get()), INVALID_HANDLE);

  const Api::IoCallBoolResult result1 = file->open(DefaultFlags);
  const filesystem_os_id_t initial_fd = getFd(file.get());
  EXPECT_TRUE(result1.return_value_);
  EXPECT_TRUE(file->isOpen());

  // check that we don't leak a file descriptor
  const Api::IoCallBoolResult result2 = file->open(DefaultFlags);
  EXPECT_EQ(initial_fd, getFd(file.get()));
  EXPECT_TRUE(result2.return_value_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenBadFilePath) {
  FilePathAndType new_file_info{Filesystem::DestinationType::File, ""};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result = file->open(DefaultFlags);
  EXPECT_FALSE(result.return_value_);
}

TEST_F(FileSystemImplTest, ExistingFile) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "existing file");

  {
    FilePathAndType new_file_info{Filesystem::DestinationType::File, file_path};
    FilePtr file = file_system_.createFile(new_file_info);
    const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
    EXPECT_TRUE(open_result.return_value_);
    std::string data(" new data");
    const Api::IoCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.return_value_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ("existing file new data", contents);
}

TEST_F(FileSystemImplTest, NonExistingFile) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  {
    FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
    FilePtr file = file_system_.createFile(new_file_info);
    const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
    EXPECT_TRUE(open_result.return_value_);
    std::string data(" new data");
    const Api::IoCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.return_value_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(new_file_path);
  EXPECT_EQ(" new data", contents);
}

TEST_F(FileSystemImplTest, StdOut) {
  FilePathAndType file_info{Filesystem::DestinationType::Stdout, ""};
  FilePtr file = file_system_.createFile(file_info);
  const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
  EXPECT_TRUE(open_result.return_value_);
  EXPECT_TRUE(file->isOpen());
  std::string data(" new data\n");
  const Api::IoCallSizeResult result = file->write(data);
  EXPECT_EQ(data.length(), result.return_value_)
      << fmt::format("{}", result.err_->getErrorDetails());
}

TEST_F(FileSystemImplTest, StdErr) {
  FilePathAndType file_info{Filesystem::DestinationType::Stderr, ""};
  FilePtr file = file_system_.createFile(file_info);
  const Api::IoCallBoolResult open_result = file->open(DefaultFlags);
  EXPECT_TRUE(open_result.return_value_) << fmt::format("{}", open_result.err_->getErrorDetails());
  EXPECT_TRUE(file->isOpen());
  std::string data(" new data\n");
  const Api::IoCallSizeResult result = file->write(data);
  EXPECT_EQ(data.length(), result.return_value_)
      << fmt::format("{}", result.err_->getErrorDetails());
}

#ifdef WIN32
TEST_F(FileSystemImplTest, Win32InvalidHandleThrows) {
  FilePathAndType file_info{Filesystem::DestinationType::Stdout, ""};
  FilePtr file = file_system_.createFile(file_info);
  // We need to flush just to make sure that the write has been completed.
  auto hh = GetStdHandle(STD_OUTPUT_HANDLE);
  FlushFileBuffers(hh);
  auto original_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  EXPECT_TRUE(SetStdHandle(STD_OUTPUT_HANDLE, NULL));
  const Api::IoCallBoolResult result = file->open(DefaultFlags);
  EXPECT_FALSE(result.return_value_);
  EXPECT_TRUE(SetStdHandle(STD_OUTPUT_HANDLE, original_handle));
}
#endif

TEST_F(FileSystemImplTest, Close) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult result1 = file->open(DefaultFlags);
  EXPECT_TRUE(result1.return_value_);
  EXPECT_TRUE(file->isOpen());

  const Api::IoCallBoolResult result2 = file->close();
  EXPECT_TRUE(result2.return_value_);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(FileSystemImplTest, WriteAfterClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult bool_result1 = file->open(DefaultFlags);
  EXPECT_TRUE(bool_result1.return_value_);
  const Api::IoCallBoolResult bool_result2 = file->close();
  EXPECT_TRUE(bool_result2.return_value_);
  const Api::IoCallSizeResult size_result = file->write(" new data");
  EXPECT_EQ(-1, size_result.return_value_);
  EXPECT_EQ(IoFileError::IoErrorCode::BadFd, size_result.err_->getErrorCode());
}

TEST_F(FileSystemImplTest, NonExistingFileAndReadOnly) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  static constexpr FlagSet flag(static_cast<size_t>(Filesystem::File::Operation::Read));
  FilePathAndType new_file_info{Filesystem::DestinationType::File, new_file_path};
  FilePtr file = file_system_.createFile(new_file_info);
  const Api::IoCallBoolResult open_result = file->open(flag);
  EXPECT_FALSE(open_result.return_value_);
}

TEST_F(FileSystemImplTest, ExistingReadOnlyFileAndWrite) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "existing file");

  {
    static constexpr FlagSet flag(static_cast<size_t>(Filesystem::File::Operation::Read));
    FilePathAndType new_file_info{Filesystem::DestinationType::File, file_path};
    FilePtr file = file_system_.createFile(new_file_info);
    const Api::IoCallBoolResult open_result = file->open(flag);
    EXPECT_TRUE(open_result.return_value_);
    std::string data(" new data");
    const Api::IoCallSizeResult result = file->write(data);
    EXPECT_TRUE(result.return_value_ < 0);
#ifdef WIN32
    EXPECT_EQ(IoFileError::IoErrorCode::Permission, result.err_->getErrorCode());
#else
    EXPECT_EQ(IoFileError::IoErrorCode::BadFd, result.err_->getErrorCode());
#endif
  }

  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ("existing file", contents);
}

TEST_F(FileSystemImplTest, TestIoFileError) {
  IoFileError error1(HANDLE_ERROR_PERM);
  EXPECT_EQ(IoFileError::IoErrorCode::Permission, error1.getErrorCode());
  EXPECT_EQ(errorDetails(HANDLE_ERROR_PERM), error1.getErrorDetails());

  IoFileError error2(HANDLE_ERROR_INVALID);
  EXPECT_EQ(IoFileError::IoErrorCode::BadFd, error2.getErrorCode());
  EXPECT_EQ(errorDetails(HANDLE_ERROR_INVALID), error2.getErrorDetails());

  int not_known_error = 42;
  IoFileError error3(not_known_error);
  EXPECT_EQ(IoFileError::IoErrorCode::UnknownError, error3.getErrorCode());
}

TEST_F(FileSystemImplTest, Overwrite) {
  const std::string original = "test_envoy";
  std::string full_filename = TestEnvironment::writeStringToFileForTest("filename", original);
  EXPECT_EQ(original, TestEnvironment::readFileToStringForTest(full_filename));

  const std::string shorter = "short";
  TestEnvironment::writeStringToFileForTest("filename", shorter, false, false);
  EXPECT_EQ(shorter, TestEnvironment::readFileToStringForTest(full_filename));
}

} // namespace Filesystem
} // namespace Envoy
