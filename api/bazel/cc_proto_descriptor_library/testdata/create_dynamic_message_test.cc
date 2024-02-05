#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "bazel/cc_proto_descriptor_library/testdata/test.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)
namespace {

using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Test;

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

} // namespace
