#include "common/config/version_converter.h"

#include "common/common/assert.h"

// Protobuf reflection is on a per-scalar type basis, i.e. there are method to
// get/set uint32. So, we need to use macro magic to reduce boiler plate below
// when copying fields.
#define UPGRADE_SCALAR(type, method_fragment)                                                      \
  case Protobuf::FieldDescriptor::TYPE_##type: {                                                   \
    if (prev_field_descriptor->is_repeated()) {                                                    \
      const int field_size = prev_reflection->FieldSize(prev_message, prev_field_descriptor);      \
      for (int n = 0; n < field_size; ++n) {                                                       \
        const auto& v =                                                                            \
            prev_reflection->GetRepeated##method_fragment(prev_message, prev_field_descriptor, n); \
        target_reflection->Add##method_fragment(target_message, target_field_descriptor, v);       \
      }                                                                                            \
    } else {                                                                                       \
      const auto v = prev_reflection->Get##method_fragment(prev_message, prev_field_descriptor);   \
      target_reflection->Set##method_fragment(target_message, target_field_descriptor, v);         \
    }                                                                                              \
    break;                                                                                         \
  }

namespace Envoy {
namespace Config {

// TODO(htuch): make the unknown field validators aware of this distinguished
// field, and don't reject if present.
constexpr uint32_t DeprecatedMessageFieldNumber = 100000;

void VersionConverter::upgrade(const Protobuf::Message& prev_message,
                               Protobuf::Message& next_message) {
  // Wow, why so complicated? Could we just do this conversion with:
  //
  //   next_message.MergeFromString(prev_message.SerializeAsString())
  //
  // and then some clever mangling of the UnknownFieldSet?
  //
  // Hold your horses! There's a few reasons that the approach below has been
  // adopted:
  // 1. We can ensure all unknown fields are placed in a distinguished
  //    DeprecatedMessageFieldNumber, so that the static/dynamic proto
  //    validators that look at unknown fields are capable of knowing the
  //    difference between deprecated fields smuggled in from previous versions
  //    and fields in the new version that are genuinely unknown by the Envoy.
  // 2. We can do proto wire breaking changes between major versions. An example
  //    of this is promotion/demotion between wrapped (e.g.
  //    google.protobuf.UInt32) and unwrapped types (e.g. uint32). This isn't
  //    done below yet, but should be possible to automate via "next version"
  //    annotations on fields.
  const Protobuf::Descriptor* next_descriptor = next_message.GetDescriptor();
  const Protobuf::Reflection* prev_reflection = prev_message.GetReflection();
  std::vector<const Protobuf::FieldDescriptor*> prev_field_descriptors;
  prev_reflection->ListFields(prev_message, &prev_field_descriptors);
  Protobuf::DynamicMessageFactory dmf;
  std::unique_ptr<Protobuf::Message> deprecated_message;

  // Iterate over all the set fields in the previous version message.
  for (const auto* prev_field_descriptor : prev_field_descriptors) {
    const Protobuf::Reflection* target_reflection = next_message.GetReflection();
    Protobuf::Message* target_message = &next_message;

    // Does the field exist in the new version message?
    const std::string& prev_name = prev_field_descriptor->name();
    const auto* target_field_descriptor = next_descriptor->FindFieldByName(prev_name);
    // If we can't find this field in the next version, it must be deprecated.
    // So, use deprecated_message and its reflection instead.
    if (target_field_descriptor == nullptr) {
      ASSERT(prev_field_descriptor->options().deprecated());
      if (!deprecated_message) {
        deprecated_message.reset(dmf.GetPrototype(prev_message.GetDescriptor())->New());
      }
      target_field_descriptor = prev_field_descriptor;
      target_reflection = deprecated_message->GetReflection();
      target_message = deprecated_message.get();
    }
    ASSERT(target_field_descriptor != nullptr);

    // These properties are guaranteed by protoxform.
    ASSERT(prev_field_descriptor->type() == target_field_descriptor->type());
    ASSERT(prev_field_descriptor->number() == target_field_descriptor->number());
    ASSERT(prev_field_descriptor->type_name() == target_field_descriptor->type_name());
    ASSERT(prev_field_descriptor->is_repeated() == target_field_descriptor->is_repeated());

    // Message fields need special handling, as we need to recurse.
    if (prev_field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
      if (prev_field_descriptor->is_repeated()) {
        const int field_size = prev_reflection->FieldSize(prev_message, prev_field_descriptor);
        for (int n = 0; n < field_size; ++n) {
          const Protobuf::Message& prev_nested_message =
              prev_reflection->GetRepeatedMessage(prev_message, prev_field_descriptor, n);
          Protobuf::Message* target_nested_message =
              target_reflection->AddMessage(target_message, target_field_descriptor);
          upgrade(prev_nested_message, *target_nested_message);
        }
      } else {
        const Protobuf::Message& prev_nested_message =
            prev_reflection->GetMessage(prev_message, prev_field_descriptor);
        Protobuf::Message* target_nested_message =
            target_reflection->MutableMessage(target_message, target_field_descriptor);
        upgrade(prev_nested_message, *target_nested_message);
      }
    } else {
      // Scalar types.
      switch (prev_field_descriptor->type()) {
        UPGRADE_SCALAR(STRING, String)
        UPGRADE_SCALAR(BYTES, String)
        UPGRADE_SCALAR(INT32, Int32)
        UPGRADE_SCALAR(INT64, Int64)
        UPGRADE_SCALAR(UINT32, UInt32)
        UPGRADE_SCALAR(UINT64, UInt64)
        UPGRADE_SCALAR(DOUBLE, Double)
        UPGRADE_SCALAR(FLOAT, Float)
        UPGRADE_SCALAR(BOOL, Bool)
        UPGRADE_SCALAR(ENUM, EnumValue)
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
      }
    }
  }

  if (deprecated_message) {
    const Protobuf::Reflection* next_reflection = next_message.GetReflection();
    auto* unknown_field_set = next_reflection->MutableUnknownFields(&next_message);
    ASSERT(unknown_field_set->empty());
    std::string* s = unknown_field_set->AddLengthDelimited(DeprecatedMessageFieldNumber);
    deprecated_message->SerializeToString(s);
  }
}

void VersionConverter::unpackDeprecated(const Protobuf::Message& upgraded_message,
                                        Protobuf::Message& deprecated_message) {
  const Protobuf::Reflection* reflection = upgraded_message.GetReflection();
  const auto& unknown_field_set = reflection->GetUnknownFields(upgraded_message);
  ASSERT(unknown_field_set.field_count() == 1);
  const auto& unknown_field = unknown_field_set.field(0);
  ASSERT(unknown_field.number() == DeprecatedMessageFieldNumber);
  const std::string& s = unknown_field.length_delimited();
  deprecated_message.ParseFromString(s);
}

} // namespace Config
} // namespace Envoy
