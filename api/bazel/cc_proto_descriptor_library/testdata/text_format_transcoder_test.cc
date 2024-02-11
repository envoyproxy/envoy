// Copyright 2019 Google Inc. All Rights Reserved.
// Author: kmensah@google.com (Kwasi Mensah)

#include <string>

#include "absl/strings/substitute.h"
#include "bazel/cc_proto_descriptor_library/testdata/test-extension.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test-extension_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test1_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/testdata/test_descriptor.pb.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)
namespace {

using google::protobuf::io::ColumnNumber;

class StringErrorCollector : public google::protobuf::io::ErrorCollector {
public:
  // String error_text is unowned and must remain valid during the use of
  // StringErrorCollector.
  // If one_indexing is set to true, all line and column numbers will be
  // increased by one for cases when provided indices are 0-indexed and
  // 1-indexed error messages are desired.
  // If ignore_warnings is set to true, warnings are discarded.
  explicit StringErrorCollector(std::string& error_text, bool one_indexing = false,
                                bool ignore_warnings = false);

  // google::protobuf::io::ErrorCollector:
  void AddError(int line, ColumnNumber column, const std::string& message) override;
  void AddWarning(int line, ColumnNumber column, const std::string& message) override;

private:
  std::string& error_text_;
  const int index_offset_;
  const bool ignore_warnings_;
};

StringErrorCollector::StringErrorCollector(std::string& error_text, bool one_indexing,
                                           bool ignore_warnings)
    : error_text_(error_text), index_offset_(one_indexing ? 1 : 0),
      ignore_warnings_(ignore_warnings) {}

void StringErrorCollector::AddError(int line, ColumnNumber column, const std::string& message) {
  absl::SubstituteAndAppend(&error_text_, "$0($1): $2\n", line + index_offset_,
                            column + index_offset_, message);
}

void StringErrorCollector::AddWarning(int line, ColumnNumber column, const std::string& message) {
  if (ignore_warnings_) {
    return;
  }
  AddError(line, column, message);
}

using ::testing::Eq;
using ::testing::Test;

TEST(TextFormatTranscoderTest, TextFormatWorks) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer;
  reserializer.loadFileDescriptors(
      protobuf::reflection::bazel_cc_proto_descriptor_library_testdata_test::kFileDescriptorInfo);

  testdata::dynamic_descriptors::Foo concrete_message;
  ASSERT_TRUE(reserializer.parseInto(R"text(
bar: "hello world"
)text",
                                     &concrete_message));

  ASSERT_THAT(concrete_message.bar(), Eq("hello world"));
}

TEST(TextToBinaryReserializerTest, TextFormatWithExtensionWorks) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer;
  const auto& file_descriptor_info = protobuf::reflection::
      bazel_cc_proto_descriptor_library_testdata_test_extension::kFileDescriptorInfo;
  reserializer.loadFileDescriptors(file_descriptor_info);

  std::string error_text;
  StringErrorCollector error_collector(error_text);
  testdata::dynamic_descriptors::Foo concrete_message;
  ASSERT_TRUE(reserializer.parseInto(R"text(
bar: "hello world"
[testdata.dynamic_descriptors.number]: 20
)text",
                                     &concrete_message, &error_collector));

  ASSERT_THAT(error_text, Eq(""));

  ASSERT_THAT(concrete_message.bar(), Eq("hello world"));
  ASSERT_THAT(concrete_message.GetExtension(testdata::dynamic_descriptors::number), Eq(20));
}

} // namespace
