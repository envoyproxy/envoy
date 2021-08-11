#include <chrono>
#include <filesystem>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
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
  Api::SysCallStringResult canonicalPath(const std::string& path) {
    return file_system_.canonicalPath(path);
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

  EXPECT_EQ(data, file_system_.fileReadToEnd(file_path));
}

// Files are read into std::string; verify that all bytes (e.g., non-ascii characters) come back
// unmodified
TEST_F(FileSystemImplTest, FileReadToEndSuccessBinary) {
  std::string data;
  for (unsigned i = 0; i < 256; i++) {
    data.push_back(i);
  }
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  const std::string read = file_system_.fileReadToEnd(file_path);
  const std::vector<uint8_t> binary_read(read.begin(), read.end());
  EXPECT_EQ(binary_read.size(), 256);
  for (unsigned i = 0; i < 256; i++) {
    EXPECT_EQ(binary_read.at(i), i);
  }
}

TEST_F(FileSystemImplTest, FileReadToEndDoesNotExist) {
  unlink(TestEnvironment::temporaryPath("envoy_this_not_exist").c_str());
  EXPECT_THROW(file_system_.fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist")),
               EnvoyException);
}

TEST_F(FileSystemImplTest, FileReadToEndNotReadable) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  std::filesystem::permissions(file_path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::remove);
  EXPECT_THROW(file_system_.fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist")),
               EnvoyException);
}

TEST_F(FileSystemImplTest, FileReadToEndDenylisted) {
  EXPECT_THROW(file_system_.fileReadToEnd("/dev/urandom"), EnvoyException);
  EXPECT_THROW(file_system_.fileReadToEnd("/proc/cpuinfo"), EnvoyException);
  EXPECT_THROW(file_system_.fileReadToEnd("/sys/block/sda/dev"), EnvoyException);
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
  result = file_system_.splitPathFromFilename("/foo/bar/baz");
  EXPECT_EQ(result.directory_, "/foo/bar");
  EXPECT_EQ(result.file_, "baz");
  result = file_system_.splitPathFromFilename("/foo/bar");
  EXPECT_EQ(result.directory_, "/foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("/foo");
  EXPECT_EQ(result.directory_, "/");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("/");
  EXPECT_EQ(result.directory_, "/");
  EXPECT_EQ(result.file_, "");
  EXPECT_THROW(file_system_.splitPathFromFilename("nopathdelimeter"), EnvoyException);
#ifdef WIN32
  result = file_system_.splitPathFromFilename("c:\\foo/bar");
  EXPECT_EQ(result.directory_, "c:\\foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("c:/foo\\bar");
  EXPECT_EQ(result.directory_, "c:/foo");
  EXPECT_EQ(result.file_, "bar");
  result = file_system_.splitPathFromFilename("c:\\foo");
  EXPECT_EQ(result.directory_, "c:\\");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("c:foo");
  EXPECT_EQ(result.directory_, "c:");
  EXPECT_EQ(result.file_, "foo");
  result = file_system_.splitPathFromFilename("c:");
  EXPECT_EQ(result.directory_, "c:");
  EXPECT_EQ(result.file_, "");
  result = file_system_.splitPathFromFilename("\\\\?\\C:\\");
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

} // namespace Filesystem
} // namespace Envoy
