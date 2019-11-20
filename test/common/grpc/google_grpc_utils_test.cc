#include "envoy/common/platform.h"

#include "common/grpc/google_grpc_utils.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

TEST(GoogleGrpcUtilsTest, MakeBufferInstanceEmpty) {
  grpc::ByteBuffer byte_buffer;
  GoogleGrpcUtils::makeBufferInstance(byte_buffer);
}

TEST(GoogleGrpcUtilsTest, MakeByteBufferEmpty) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
  buffer = nullptr;
  GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
}

TEST(GoogleGrpcUtilsTest, MakeBufferInstance1) {
  grpc::Slice slice("test");
  grpc::ByteBuffer byte_buffer(&slice, 1);
  auto buffer_instance = GoogleGrpcUtils::makeBufferInstance(byte_buffer);
  EXPECT_EQ(buffer_instance->toString(), "test");
}

// Test building a Buffer::Instance from 3 grpc::Slice(s).
TEST(GoogleGrpcUtilsTest, MakeBufferInstance3) {
  std::array<grpc::Slice, 3> slices = {
      {grpc::string("test"), grpc::string(" "), grpc::string("this")}};
  grpc::ByteBuffer byte_buffer(&slices[0], 3);
  auto buffer_instance = GoogleGrpcUtils::makeBufferInstance(byte_buffer);
  EXPECT_EQ(buffer_instance->toString(), "test this");
}

TEST(GoogleGrpcUtilsTest, MakeByteBuffer1) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  auto byte_buffer = GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
  std::vector<grpc::Slice> slices;
  RELEASE_ASSERT(byte_buffer.Dump(&slices).ok(), "");
  std::string str;
  for (auto& s : slices) {
    str.append(std::string(reinterpret_cast<const char*>(s.begin()), s.size()));
  }
  EXPECT_EQ(str, "test");
}

// Test building a grpc::ByteBuffer from a Buffer::Instance with 3 slices.
TEST(GoogleGrpcUtilsTest, MakeByteBuffer3) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  Buffer::BufferFragmentImpl f1("test", 4, nullptr);
  buffer->addBufferFragment(f1);
  Buffer::BufferFragmentImpl f2(" ", 1, nullptr);
  buffer->addBufferFragment(f2);
  Buffer::BufferFragmentImpl f3("this", 4, nullptr);
  buffer->addBufferFragment(f3);
  auto byte_buffer = GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
  std::vector<grpc::Slice> slices;
  RELEASE_ASSERT(byte_buffer.Dump(&slices).ok(), "");
  std::string str;
  for (auto& s : slices) {
    str.append(std::string(reinterpret_cast<const char*>(s.begin()), s.size()));
  }
  EXPECT_EQ(str, "test this");
}

// Test building a Buffer::Instance from a grpc::ByteBuffer from a Buffer::Instance with 3 slices.
TEST(GoogleGrpcUtilsTest, ByteBufferInstanceRoundTrip) {
  std::array<grpc::Slice, 3> slices = {
      {grpc::string("test"), grpc::string(" "), grpc::string("this")}};
  grpc::ByteBuffer byte_buffer(&slices[0], 3);
  auto buffer_instance1 = GoogleGrpcUtils::makeBufferInstance(byte_buffer);
  auto byte_buffer2 = GoogleGrpcUtils::makeByteBuffer(std::move(buffer_instance1));
  auto buffer_instance2 = GoogleGrpcUtils::makeBufferInstance(byte_buffer2);
  EXPECT_EQ(buffer_instance2->toString(), "test this");
}

} // namespace
} // namespace Grpc
} // namespace Envoy
