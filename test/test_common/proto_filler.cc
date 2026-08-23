#include "test/test_common/proto_filler.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace ProtoFiller {

namespace {

using Field = Protobuf::FieldDescriptor;

void fillMessage(Protobuf::Message& message, const Options& options, uint32_t depth);

const Protobuf::EnumValueDescriptor& nonDefaultEnumValue(const Protobuf::EnumDescriptor& enum_type,
                                                         uint32_t index) {
  for (int i = 0; i < enum_type.value_count(); i++) {
    const Protobuf::EnumValueDescriptor& enum_value =
        *enum_type.value((index + i) % enum_type.value_count());
    if (enum_value.number() != 0) {
      return enum_value;
    }
  }
  return *enum_type.value(0);
}

bool isAny(const Field& field) {
  return field.message_type()->full_name() == "google.protobuf.Any";
}

void packAny(Protobuf::Message& any, const Field& field, const Options& options, uint32_t depth) {
  const auto packed_type = options.any_types.find(field.name());
  if (packed_type == options.any_types.end()) {
    return;
  }
  ProtobufTypes::MessagePtr packed(packed_type->second->New());
  fillMessage(*packed, options, depth);
  std::ignore = dynamic_cast<Protobuf::Any&>(any).PackFrom(*packed);
}

void fillField(Protobuf::Message& message, const Field& field, const Options& options,
               uint32_t depth) {
  const Protobuf::Reflection& reflection = *message.GetReflection();
  const bool repeated = field.is_repeated();
  const uint32_t elements = repeated ? options.elements : 1;
  for (uint32_t index = 0; index < elements; index++) {
    const uint32_t value = field.number() + index;
    switch (field.cpp_type()) {
    case Field::CPPTYPE_INT32:
      repeated ? reflection.AddInt32(&message, &field, value)
               : reflection.SetInt32(&message, &field, value);
      break;
    case Field::CPPTYPE_UINT32:
      repeated ? reflection.AddUInt32(&message, &field, value)
               : reflection.SetUInt32(&message, &field, value);
      break;
    case Field::CPPTYPE_INT64:
      repeated ? reflection.AddInt64(&message, &field, value)
               : reflection.SetInt64(&message, &field, value);
      break;
    case Field::CPPTYPE_UINT64:
      repeated ? reflection.AddUInt64(&message, &field, value)
               : reflection.SetUInt64(&message, &field, value);
      break;
    case Field::CPPTYPE_DOUBLE:
      repeated ? reflection.AddDouble(&message, &field, value + 0.5)
               : reflection.SetDouble(&message, &field, value + 0.5);
      break;
    case Field::CPPTYPE_FLOAT:
      repeated ? reflection.AddFloat(&message, &field, value + 0.25f)
               : reflection.SetFloat(&message, &field, value + 0.25f);
      break;
    case Field::CPPTYPE_BOOL:
      repeated ? reflection.AddBool(&message, &field, true)
               : reflection.SetBool(&message, &field, true);
      break;
    case Field::CPPTYPE_ENUM: {
      const Protobuf::EnumValueDescriptor& enum_value =
          nonDefaultEnumValue(*field.enum_type(), index);
      repeated ? reflection.AddEnum(&message, &field, &enum_value)
               : reflection.SetEnum(&message, &field, &enum_value);
      break;
    }
    case Field::CPPTYPE_STRING: {
      const std::string text = absl::StrCat(field.name(), "_", index);
      repeated ? reflection.AddString(&message, &field, text)
               : reflection.SetString(&message, &field, text);
      break;
    }
    case Field::CPPTYPE_MESSAGE: {
      if (field.is_map()) {
        Protobuf::Message& entry = *reflection.AddMessage(&message, &field);
        fillField(entry, *field.message_type()->map_key(), options, depth + 1);
        fillField(entry, *field.message_type()->map_value(), options, depth + 1);
        break;
      }
      Protobuf::Message& nested = repeated ? *reflection.AddMessage(&message, &field)
                                           : *reflection.MutableMessage(&message, &field);
      if (isAny(field)) {
        packAny(nested, field, options, depth + 1);
        break;
      }
      fillMessage(nested, options, depth + 1);
      break;
    }
    }
  }
}

void fillMessage(Protobuf::Message& message, const Options& options, uint32_t depth) {
  if (depth >= options.max_depth) {
    return;
  }
  const Protobuf::Descriptor& descriptor = *message.GetDescriptor();
  for (int i = 0; i < descriptor.field_count(); i++) {
    const Field& field = *descriptor.field(i);
    // Only the first field of a oneof, as the others would overwrite it.
    if (field.containing_oneof() != nullptr && field.index_in_oneof() != 0) {
      continue;
    }
    fillField(message, field, options, depth);
  }
}

} // namespace

void fill(Protobuf::Message& message, const Options& options) { fillMessage(message, options, 0); }

} // namespace ProtoFiller
} // namespace Envoy
