#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base_descriptor.pb.h"

#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NotNull;

namespace Envoy {

TEST(ProtoDescriptorLibrary, CreateDynamicMessage) {
  cc_proto_descriptor_library::TextFormatTranscoder reserializer;
  reserializer.loadFileDescriptors(
      protobuf::reflection::envoy_config_core_v3_base::kFileDescriptorInfo);

  envoy::config::core::v3::Locality concrete_message;
  concrete_message.set_region("Region");
  concrete_message.set_zone("Zone");
  concrete_message.set_sub_zone("Subzone");

  auto dynamic_message =
      cc_proto_descriptor_library::createDynamicMessage(reserializer, concrete_message);
  ASSERT_THAT(dynamic_message, NotNull());

  // Access the descriptor.
  auto descriptor = dynamic_message->GetDescriptor();
  ASSERT_THAT(descriptor, NotNull());
  ASSERT_THAT(descriptor->full_name(), Eq(concrete_message.GetTypeName()));

  // Access reflection.
  auto reflection = dynamic_message->GetReflection();
  const auto bar_feld_descriptor = descriptor->FindFieldByName("region");
  ASSERT_THAT(bar_feld_descriptor, NotNull());
  ASSERT_THAT(reflection->GetString(*dynamic_message, bar_feld_descriptor), Eq("Region"));
}

} // namespace Envoy
