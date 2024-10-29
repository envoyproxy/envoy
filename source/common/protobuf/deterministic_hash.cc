#if defined(ENVOY_ENABLE_FULL_PROTOS)
#include "source/common/protobuf/deterministic_hash.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"

namespace Envoy {
namespace DeterministicProtoHash {
namespace {

// Get a scalar field from protobuf reflection field definition. The return
// type must be specified by the caller. Every implementation is a specialization
// because the reflection interface did separate named functions instead of a
// template.
template <typename T>
T reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                const Protobuf::FieldDescriptor& field);

template <>
uint32_t reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                       const Protobuf::FieldDescriptor& field) {
  return reflection.GetUInt32(message, &field);
}

template <>
int32_t reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                      const Protobuf::FieldDescriptor& field) {
  return reflection.GetInt32(message, &field);
}

template <>
uint64_t reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                       const Protobuf::FieldDescriptor& field) {
  return reflection.GetUInt64(message, &field);
}

template <>
int64_t reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                      const Protobuf::FieldDescriptor& field) {
  return reflection.GetInt64(message, &field);
}

template <>
float reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                    const Protobuf::FieldDescriptor& field) {
  return reflection.GetFloat(message, &field);
}

template <>
double reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                     const Protobuf::FieldDescriptor& field) {
  return reflection.GetDouble(message, &field);
}

template <>
bool reflectionGet(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                   const Protobuf::FieldDescriptor& field) {
  return reflection.GetBool(message, &field);
}

// Takes a field of scalar type, and hashes it. In case the field is a repeated field,
// the function hashes each of its elements.
template <typename T, std::enable_if_t<std::is_scalar_v<T>, bool> = true>
uint64_t hashScalarField(const Protobuf::Reflection& reflection, const Protobuf::Message& message,
                         const Protobuf::FieldDescriptor& field, uint64_t seed) {
  if (field.is_repeated()) {
    for (const T& scalar : reflection.GetRepeatedFieldRef<T>(message, &field)) {
      seed = HashUtil::xxHash64Value(scalar, seed);
    }
  } else {
    seed = HashUtil::xxHash64Value(reflectionGet<T>(reflection, message, field), seed);
  }
  return seed;
}

uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed = 0);
uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor& field, uint64_t seed);

// To make a map serialize deterministically we need to ignore the order of
// the map fields. To do that, we simply combine the hashes of each entry
// using an unordered operator (addition), and then apply that combined hash to
// the seed.
uint64_t reflectionHashMapField(const Protobuf::Message& message,
                                const Protobuf::FieldDescriptor& field, uint64_t seed) {
  const Protobuf::Reflection& reflection = *message.GetReflection();
  ASSERT(field.is_map());
  const auto& entries = reflection.GetRepeatedFieldRef<Protobuf::Message>(message, &field);
  ASSERT(!entries.empty());
  const Protobuf::Descriptor& map_descriptor = *entries.begin()->GetDescriptor();
  const Protobuf::FieldDescriptor& key_field = *map_descriptor.map_key();
  const Protobuf::FieldDescriptor& value_field = *map_descriptor.map_value();
  uint64_t combined_hash = 0;
  for (const Protobuf::Message& entry : entries) {
    uint64_t entry_hash = reflectionHashField(entry, key_field, 0);
    entry_hash = reflectionHashField(entry, value_field, entry_hash);
    combined_hash += entry_hash;
  }
  return HashUtil::xxHash64Value(combined_hash, seed);
}

uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor& field, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const Protobuf::Reflection& reflection = *message.GetReflection();
  seed = HashUtil::xxHash64Value(field.number(), seed);
  switch (field.cpp_type()) {
  case FieldDescriptor::CPPTYPE_INT32:
    seed = hashScalarField<int32_t>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_UINT32:
    seed = hashScalarField<uint32_t>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_INT64:
    seed = hashScalarField<int64_t>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_UINT64:
    seed = hashScalarField<uint64_t>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_DOUBLE:
    seed = hashScalarField<double>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_FLOAT:
    seed = hashScalarField<float>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_BOOL:
    seed = hashScalarField<bool>(reflection, message, field, seed);
    break;
  case FieldDescriptor::CPPTYPE_ENUM:
    if (field.is_repeated()) {
      const int c = reflection.FieldSize(message, &field);
      for (int i = 0; i < c; i++) {
        seed = HashUtil::xxHash64Value(reflection.GetRepeatedEnumValue(message, &field, i), seed);
      }
    } else {
      seed = HashUtil::xxHash64Value(reflection.GetEnumValue(message, &field), seed);
    }
    break;
  case FieldDescriptor::CPPTYPE_STRING:
    if (field.is_repeated()) {
      for (const std::string& str : reflection.GetRepeatedFieldRef<std::string>(message, &field)) {
        seed = HashUtil::xxHash64(str, seed);
      }
    } else {
      // Scratch may be used by GetStringReference if the field is not already a std::string.
      std::string scratch;
      seed = HashUtil::xxHash64(reflection.GetStringReference(message, &field, &scratch), seed);
    }
    break;
  case FieldDescriptor::CPPTYPE_MESSAGE:
    if (field.is_map()) {
      seed = reflectionHashMapField(message, field, seed);
    } else if (field.is_repeated()) {
      for (const Protobuf::Message& submsg :
           reflection.GetRepeatedFieldRef<Protobuf::Message>(message, &field)) {
        seed = reflectionHashMessage(submsg, seed);
      }
    } else {
      seed = reflectionHashMessage(reflection.GetMessage(message, &field), seed);
    }
    break;
  }
  return seed;
}

// Converts from type urls OR descriptor full names to descriptor full names.
// Type urls are as used in envoy yaml config, e.g.
// "type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig"
// becomes
// "envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig"
absl::string_view typeUrlToDescriptorFullName(absl::string_view url) {
  const size_t pos = url.rfind('/');
  if (pos != absl::string_view::npos) {
    return url.substr(pos + 1);
  }
  return url;
}

std::unique_ptr<Protobuf::Message> unpackAnyForReflection(const ProtobufWkt::Any& any) {
  const Protobuf::Descriptor* descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          typeUrlToDescriptorFullName(any.type_url()));
  // If the type name refers to an unknown type, we treat it the same as other
  // unknown fields - not including its contents in the hash.
  if (descriptor == nullptr) {
    return nullptr;
  }
  const Protobuf::Message* prototype =
      Protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
  ASSERT(prototype != nullptr, "should be impossible since the descriptor is known");
  std::unique_ptr<Protobuf::Message> msg(prototype->New());
  any.UnpackTo(msg.get());
  return msg;
}

// This is intentionally ignoring unknown fields.
uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const Protobuf::Reflection* reflection = message.GetReflection();
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  seed = HashUtil::xxHash64(descriptor->full_name(), seed);
  if (descriptor->well_known_type() == Protobuf::Descriptor::WELLKNOWNTYPE_ANY) {
    const ProtobufWkt::Any* any = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(&message);
    ASSERT(any != nullptr, "casting to any should always work for WELLKNOWNTYPE_ANY");
    std::unique_ptr<Protobuf::Message> submsg = unpackAnyForReflection(*any);
    if (submsg == nullptr) {
      // If we wanted to handle unknown types in Any, this is where we'd have to do it.
      // Since we don't know the type to introspect it, we hash just its type name.
      return HashUtil::xxHash64(any->type_url(), seed);
    }
    return reflectionHashMessage(*submsg, seed);
  }
  std::vector<const FieldDescriptor*> fields;
  // ListFields returned the fields ordered by field number.
  reflection->ListFields(message, &fields);
  // If we wanted to handle unknown fields, we'd need to also GetUnknownFields here.
  for (const FieldDescriptor* field : fields) {
    seed = reflectionHashField(message, *field, seed);
  }
  // Hash one extra character to signify end of message, so that
  // msg{} field2=2
  // hashes differently from
  // msg{field2=2}
  return HashUtil::xxHash64("\x17", seed);
}
} // namespace

uint64_t hash(const Protobuf::Message& message) { return reflectionHashMessage(message, 0); }

} // namespace DeterministicProtoHash
} // namespace Envoy
#endif
