// Copyright 2019 Google Inc. All Rights Reserved.
// Author: kmensah@google.com (Kwasi Mensah)

#include "bazel/cc_proto_descriptor_library/testdata/test.pb.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

// NOLINT(namespace-envoy)
namespace {

using ::testing::Eq;
using ::testing::Test;

TEST(TextFormatTranscoderTest, GlobalFallbackAllowed) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer(
      /*allow_global_fallback=*/true);

  testdata::dynamic_descriptors::Foo concrete_message;
  ASSERT_TRUE(reserializer.parseInto(R"text(
bar: "hello world"
)text",
                                     &concrete_message));

  ASSERT_THAT(concrete_message.bar(), Eq("hello world"));
}

TEST(TextFormatTranscoderTest, GlobalFallbackDisallowed) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer(
      /*allow_global_fallback=*/false);

  testdata::dynamic_descriptors::Foo concrete_message;
  ASSERT_FALSE(reserializer.parseInto(R"text(
bar: "hello world"
)text",
                                      &concrete_message));
}

} // namespace
