// Tests that the descriptors embedded by file_descriptor_generator survive the trip through
// generated C++ code byte for byte, including bytes that need escaping and descriptors too large
// for a single string literal.

#include <string>

#include "absl/strings/string_view.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
#include "bazel/cc_proto_descriptor_library/testdata/test-raw-bytes.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test-raw-bytes_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test_large.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test_large_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)
namespace {

using ::cc_proto_descriptor_library::TextFormatTranscoder;
using ::cc_proto_descriptor_library::internal::FileDescriptorInfo;
using ::google::protobuf::util::MessageDifferencer;
using ::std::string_literals::operator""s;
using ::testdata::dynamic_descriptors::LargeMessage;
using ::testdata::dynamic_descriptors::RawBytesMessage;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::NotNull;

const FileDescriptorInfo& rawBytesFileDescriptorInfo() {
  return protobuf::reflection::bazel_cc_proto_descriptor_library_testdata_test_raw_bytes::
      kFileDescriptorInfo;
}

const FileDescriptorInfo& largeFileDescriptorInfo() {
  return protobuf::reflection::bazel_cc_proto_descriptor_library_testdata_test_large::
      kFileDescriptorInfo;
}

// Returns the FileDescriptorProto that the C++ proto library was compiled from, for comparison
// with the embedded bytes.
google::protobuf::FileDescriptorProto
compiledFileDescriptorProto(const google::protobuf::Descriptor& message) {
  google::protobuf::FileDescriptorProto file_proto;
  message.file()->CopyTo(&file_proto);
  message.file()->CopyJsonNameTo(&file_proto);
  return file_proto;
}

TEST(FileDescriptorGeneratorTest, EmbeddedBytesNeedEscaping) {
  // Checks that test-raw-bytes.proto still exercises the interesting cases.
  const absl::string_view bytes = rawBytesFileDescriptorInfo().file_descriptor_bytes;
  EXPECT_THAT(bytes, HasSubstr("?\?/"));
  EXPECT_THAT(bytes, HasSubstr("a\0b\0012"s));
  EXPECT_THAT(bytes, HasSubstr("h\303\251llo"));
  EXPECT_THAT(bytes, HasSubstr("\" ' \\"));
}

TEST(FileDescriptorGeneratorTest, EmbeddedBytesMatchCompiledDescriptor) {
  google::protobuf::FileDescriptorProto file_proto;
  ASSERT_TRUE(file_proto.ParseFromString(rawBytesFileDescriptorInfo().file_descriptor_bytes));

  google::protobuf::FileDescriptorProto expected =
      compiledFileDescriptorProto(*RawBytesMessage::descriptor());
  EXPECT_TRUE(MessageDifferencer::Equals(file_proto, expected))
      << "embedded: " << file_proto.DebugString() << "\ncompiled: " << expected.DebugString();
}

TEST(FileDescriptorGeneratorTest, EmbeddedBytesBuildInDescriptorPool) {
  google::protobuf::FileDescriptorProto file_proto;
  ASSERT_TRUE(file_proto.ParseFromString(rawBytesFileDescriptorInfo().file_descriptor_bytes));
  google::protobuf::DescriptorPool pool;
  ASSERT_THAT(pool.BuildFile(file_proto), NotNull());

  const google::protobuf::Descriptor* message =
      pool.FindMessageTypeByName("testdata.dynamic_descriptors.RawBytesMessage");
  ASSERT_THAT(message, NotNull());
  EXPECT_EQ(message->FindFieldByName("trigraphs")->default_value_string(),
            "?\?= ?\?/ ?\?' ?\?( ?\?) ?\?! ?\?< ?\?> ?\?-");
  EXPECT_EQ(message->FindFieldByName("nul_bytes")->default_value_string(), "a\0b\0012"s);
  EXPECT_EQ(message->FindFieldByName("non_ascii")->default_value_string(),
            "h\303\251llo \342\230\203");
  EXPECT_EQ(message->FindFieldByName("quotes")->default_value_string(), "\" ' \\");
}

TEST(FileDescriptorGeneratorTest, TextFormatTranscoderUsesEmbeddedBytes) {
  // Without the global fallback, parsing can only succeed using the embedded descriptor.
  TextFormatTranscoder transcoder(/*allow_global_fallback=*/false);
  transcoder.loadFileDescriptors(rawBytesFileDescriptorInfo());

  RawBytesMessage message;
  ASSERT_TRUE(transcoder.parseInto(R"pb(nul_bytes: "x\000y")pb", &message));

  EXPECT_EQ(message.nul_bytes(), "x\0y"s);
}

TEST(FileDescriptorGeneratorTest, LargeDescriptorMatchesCompiledDescriptor) {
  const absl::string_view bytes = largeFileDescriptorInfo().file_descriptor_bytes;
  // Too large for one string literal, so the generator emits a char array.
  ASSERT_THAT(bytes.size(), Gt(65535));

  google::protobuf::FileDescriptorProto file_proto;
  ASSERT_TRUE(file_proto.ParseFromString(bytes));

  google::protobuf::FileDescriptorProto expected =
      compiledFileDescriptorProto(*LargeMessage::descriptor());
  EXPECT_TRUE(MessageDifferencer::Equals(file_proto, expected));
}

TEST(FileDescriptorGeneratorTest, TextFormatTranscoderUsesLargeDescriptor) {
  TextFormatTranscoder transcoder(/*allow_global_fallback=*/false);
  transcoder.loadFileDescriptors(largeFileDescriptorInfo());

  LargeMessage message;
  ASSERT_TRUE(transcoder.parseInto(R"pb(special: "x\000y")pb", &message));

  EXPECT_EQ(message.special(), "x\0y"s);
}

} // namespace
