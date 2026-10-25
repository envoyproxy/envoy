#include <string>

#include "absl/strings/escaping.h"
#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
#include "bazel/cc_proto_descriptor_library/testdata/test.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "google/protobuf/descriptor.pb.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)
namespace {

using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Test;

class ParseErrorCollector : public google::protobuf::io::ErrorCollector {
public:
  void RecordError(int line, int column, absl::string_view message) override {
    EXPECT_EQ(line, 0);
    EXPECT_EQ(column, 0);
    EXPECT_EQ(message, "Could not parse dynamic message for: testdata.dynamic_descriptors.Foo");
    ++errors_;
  }
  void RecordWarning(int, int, absl::string_view) override { ADD_FAILURE(); }

  int errors_ = 0;
};

TEST(TextFormatTranscoderTest, CreateDynamicMessage) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer;
  reserializer.loadFileDescriptors(
      protobuf::reflection::bazel_cc_proto_descriptor_library_testdata_test::kFileDescriptorInfo);

  testdata::dynamic_descriptors::Foo concrete_message;
  concrete_message.set_bar("hello world");
  auto dynamic_message =
      cc_proto_descriptor_library::createDynamicMessage(reserializer, concrete_message);
  ASSERT_THAT(dynamic_message, NotNull());

  // Access the descriptor.
  auto descriptor = dynamic_message->GetDescriptor();
  ASSERT_THAT(descriptor, NotNull());
  ASSERT_THAT(descriptor->full_name(), Eq(concrete_message.GetTypeName()));

  // Access reflection.
  auto reflection = dynamic_message->GetReflection();
  const auto bar_feld_descriptor = descriptor->FindFieldByName("bar");
  ASSERT_THAT(bar_feld_descriptor, NotNull());
  ASSERT_THAT(reflection->GetString(*dynamic_message, bar_feld_descriptor), Eq("hello world"));
}

TEST(TextFormatTranscoderTest, CreateDynamicMessageParseFailure) {
  google::protobuf::FileDescriptorProto descriptor;
  testdata::dynamic_descriptors::Foo::descriptor()->file()->CopyTo(&descriptor);
  auto* field = descriptor.mutable_message_type(0)->mutable_field(0);
  field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
  field->set_type_name(".testdata.dynamic_descriptors.Foo");
  std::string serialized_descriptor;
  ASSERT_TRUE(descriptor.SerializeToString(&serialized_descriptor));
  const auto descriptor_bytes = absl::Base64Escape(serialized_descriptor);
  const auto& original =
      protobuf::reflection::bazel_cc_proto_descriptor_library_testdata_test::kFileDescriptorInfo;
  const cc_proto_descriptor_library::internal::FileDescriptorInfo descriptor_info{
      original.file_name, descriptor_bytes, original.deps};
  cc_proto_descriptor_library::TextFormatTranscoder reserializer(false);
  reserializer.loadFileDescriptors(descriptor_info);

  // The string is valid in the concrete message but is a truncated nested message
  // under the dynamic descriptor.
  testdata::dynamic_descriptors::Foo concrete_message;
  ASSERT_NE(cc_proto_descriptor_library::createDynamicMessage(reserializer, concrete_message),
            nullptr);
  concrete_message.set_bar("\x08");
  ParseErrorCollector error_collector;
  EXPECT_EQ(cc_proto_descriptor_library::createDynamicMessage(reserializer, concrete_message,
                                                              &error_collector),
            nullptr);
  EXPECT_EQ(error_collector.errors_, 1);
  EXPECT_EQ(cc_proto_descriptor_library::createDynamicMessage(reserializer, concrete_message),
            nullptr);
}

} // namespace
