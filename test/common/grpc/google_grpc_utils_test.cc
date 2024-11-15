#include "envoy/common/platform.h"

#include "source/common/grpc/google_grpc_utils.h"

#include "test/proto/helloworld.pb.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Pair;
using testing::UnorderedElementsAre;

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

// Validate that we build the grpc::ChannelArguments as expected.
TEST(GoogleGrpcUtilsTest, ChannelArgsFromConfig) {
  const auto config = TestUtility::parseYaml<envoy::config::core::v3::GrpcService>(R"EOF(
  google_grpc:
    channel_args:
      args:
        grpc.http2.max_pings_without_data: { int_value: 3 }
        grpc.default_authority: { string_value: foo }
        grpc.http2.max_ping_strikes: { int_value: 5 }
        grpc.ssl_target_name_override: { string_value: bar }
  )EOF");
  const grpc::ChannelArguments channel_args = GoogleGrpcUtils::channelArgsFromConfig(config);
  grpc_channel_args effective_args = channel_args.c_channel_args();
  absl::node_hash_map<std::string, std::string> string_args;
  absl::node_hash_map<std::string, int> int_args;
  for (uint32_t n = 0; n < effective_args.num_args; ++n) {
    const grpc_arg arg = effective_args.args[n];
    ASSERT_TRUE(arg.type == GRPC_ARG_STRING || arg.type == GRPC_ARG_INTEGER);
    if (arg.type == GRPC_ARG_STRING) {
      string_args[arg.key] = arg.value.string;
    } else if (arg.type == GRPC_ARG_INTEGER) {
      int_args[arg.key] = arg.value.integer;
    }
  }
  EXPECT_THAT(string_args,
              UnorderedElementsAre(Pair("grpc.ssl_target_name_override", "bar"),
                                   Pair("grpc.primary_user_agent", HasSubstr("grpc-c++/")),
                                   Pair("grpc.default_authority", "foo")));
  EXPECT_THAT(int_args, UnorderedElementsAre(Pair("grpc.http2.max_ping_strikes", 5),
                                             Pair("grpc.http2.max_pings_without_data", 3)));
}

} // namespace
} // namespace Grpc
} // namespace Envoy
