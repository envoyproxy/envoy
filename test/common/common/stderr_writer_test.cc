#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

#include "source/common/common/stderr_writer.h"

#include "gtest/gtest.h"

namespace Envoy {

class StderrWriterPeer {
public:
  using WriteFn = StderrWriter::WriteFn;
  static void flush(StderrWriter& writer, WriteFn write_fn) { writer.flush(write_fn); }
};

namespace {

// State for the stand-ins for write(2) below, which have to be plain functions.
struct FakeWrite {
  std::string output;
  int calls{0};
};

FakeWrite& fakeWrite() {
  static FakeWrite fake;
  return fake;
}

int64_t writeAll(const char* data, size_t size) {
  fakeWrite().calls++;
  fakeWrite().output.append(data, size);
  return static_cast<int64_t>(size);
}

// Accepts at most 3 bytes at a time.
int64_t writeShort(const char* data, size_t size) {
  return writeAll(data, std::min<size_t>(size, 3));
}

// Interrupted the first time it is called.
int64_t writeInterrupted(const char* data, size_t size) {
  if (fakeWrite().calls == 0) {
    fakeWrite().calls++;
    errno = EINTR;
    return -1;
  }
  return writeAll(data, size);
}

int64_t writeFails(const char*, size_t) {
  fakeWrite().calls++;
  errno = EIO;
  return -1;
}

TEST(StderrWriterTest, FormatsValues) {
  const void* address = reinterpret_cast<const void*>(uintptr_t{0x1f2e3d});
  const void* null_address = nullptr;

  testing::internal::CaptureStderr();
  StderrWriter() << "text " << absl::string_view("view") << " " << 42 << " " << -7 << " " << 0
                 << " " << address << " " << null_address << "\n";
  EXPECT_EQ("text view 42 -7 0 0x1f2e3d 0x0\n", testing::internal::GetCapturedStderr());
}

TEST(StderrWriterTest, NullText) {
  const char* text = nullptr;
  testing::internal::CaptureStderr();
  StderrWriter() << "text: " << text << "\n";
  EXPECT_EQ("text: (null)\n", testing::internal::GetCapturedStderr());
}

TEST(StderrWriterTest, WritesOnFlushAndDestruction) {
  testing::internal::CaptureStderr();
  {
    StderrWriter writer;
    writer << "one\n";
    writer.flush();
    writer << "two\n";
  }
  EXPECT_EQ("one\ntwo\n", testing::internal::GetCapturedStderr());
}

TEST(StderrWriterTest, TruncatesKeepingNewline) {
  const std::string text(StderrWriter::Capacity * 2, 'x');
  testing::internal::CaptureStderr();
  StderrWriter() << text << "\n";
  const std::string output = testing::internal::GetCapturedStderr();
  EXPECT_EQ(size_t{StderrWriter::Capacity}, output.size());
  EXPECT_EQ(std::string(StderrWriter::Capacity - 1, 'x') + "\n", output);
}

TEST(StderrWriterTest, ContinuesShortWrites) {
  fakeWrite() = {};
  StderrWriter writer;
  writer << "0123456789\n";
  StderrWriterPeer::flush(writer, &writeShort);
  EXPECT_EQ("0123456789\n", fakeWrite().output);
  EXPECT_EQ(4, fakeWrite().calls);
}

TEST(StderrWriterTest, RetriesInterruptedWrites) {
  fakeWrite() = {};
  StderrWriter writer;
  writer << "interrupted\n";
  StderrWriterPeer::flush(writer, &writeInterrupted);
  EXPECT_EQ("interrupted\n", fakeWrite().output);
  EXPECT_EQ(2, fakeWrite().calls);
}

TEST(StderrWriterTest, DropsOutputOnFailedWrite) {
  fakeWrite() = {};
  StderrWriter writer;
  writer << "lost\n";
  StderrWriterPeer::flush(writer, &writeFails);
  EXPECT_EQ("", fakeWrite().output);
  EXPECT_EQ(1, fakeWrite().calls);

  // What could not be written is not kept for the next flush.
  writer << "kept\n";
  StderrWriterPeer::flush(writer, &writeAll);
  EXPECT_EQ("kept\n", fakeWrite().output);
  EXPECT_EQ(2, fakeWrite().calls);
}

} // namespace
} // namespace Envoy
