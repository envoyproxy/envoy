#include <arpa/inet.h>

#include "common/grpc/google_grpc_utils.h"

#include "test/mocks/upstream/mocks.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {

TEST(GoogleGrpcUtilsTest, MakeBufferInstanceEmpty) {
  grpc::ByteBuffer byteBuffer;
  GoogleGrpcUtils::makeBufferInstance(byteBuffer);
}

TEST(GoogleGrpcUtilsTest, MakeByteBufferEmpty) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
}

TEST(GoogleGrpcUtilsTest, MakeBufferInstance1) {
  grpc::Slice slice("test");
  grpc::ByteBuffer byteBuffer(&slice, 1);
  auto bufferInstance = GoogleGrpcUtils::makeBufferInstance(byteBuffer);
  EXPECT_EQ(bufferInstance->toString(), "test");
}

// Test building a Buffer::Instance from 3 grpc::Slice(s).
TEST(GoogleGrpcUtilsTest, MakeBufferInstance3) {
  grpc::Slice slices[3] = {{"test"}, {" "}, {"this"}};
  grpc::ByteBuffer byteBuffer(slices, 3);
  auto bufferInstance = GoogleGrpcUtils::makeBufferInstance(byteBuffer);
  EXPECT_EQ(bufferInstance->toString(), "test this");
}

TEST(GoogleGrpcUtilsTest, MakeByteBuffer1) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  auto byteBuffer = GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
  std::vector<grpc::Slice> slices;
  byteBuffer.Dump(&slices);
  std::string str;
  for (auto& s : slices) {
    str.append(std::string(reinterpret_cast<const char*>(s.begin()), s.size()));
  }
  EXPECT_EQ(str, "test");
}

// Test building a grpc::ByteBuffer from a Bufffer::Instance with 3 slices.
TEST(GoogleGrpcUtilsTest, MakeByteBuffer3) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add("test", 4);
  buffer->add(" ", 1);
  buffer->add("this", 4);
  auto byteBuffer = GoogleGrpcUtils::makeByteBuffer(std::move(buffer));
  std::vector<grpc::Slice> slices;
  byteBuffer.Dump(&slices);
  std::string str;
  for (auto& s : slices) {
    str.append(std::string(reinterpret_cast<const char*>(s.begin()), s.size()));
  }
  EXPECT_EQ(str, "test this");
}

// Test building a Buffer::Instance from a grpc::ByteBuffer from a Bufffer::Instance with 3 slices.
TEST(GoogleGrpcUtilsTest, ByteBufferInstanceRoundTrip) {
  grpc::Slice slices[3] = {{"test"}, {" "}, {"this"}};
  grpc::ByteBuffer byteBuffer1(slices, 3);
  auto bufferInstance1 = GoogleGrpcUtils::makeBufferInstance(byteBuffer1);
  auto byteBuffer2 = GoogleGrpcUtils::makeByteBuffer(std::move(bufferInstance1));
  auto bufferInstance2 = GoogleGrpcUtils::makeBufferInstance(byteBuffer2);
  EXPECT_EQ(bufferInstance2->toString(), "test this");
}

} // namespace Grpc
} // namespace Envoy
