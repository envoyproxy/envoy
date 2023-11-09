#if defined(ENVOY_ENABLE_FULL_PROTOS)
#include "source/common/protobuf/deterministic_hash.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"

namespace Envoy {
namespace DeterministicProtoHash {
namespace {
#define REFLECTION_FOR_EACH(get_type, hash_type)                                                   \
  if (field->is_repeated()) {                                                                      \
    for (int i = 0; i < reflection->FieldSize(message, field); i++) {                              \
      hash_type(reflection->GetRepeated##get_type(message, field, i));                             \
    }                                                                                              \
  } else {                                                                                         \
    hash_type(reflection->Get##get_type(message, field));                                          \
  }

#define MAP_SORT_BY(get_type)                                                                      \
  do {                                                                                             \
    std::sort(                                                                                     \
        map.begin(), map.end(),                                                                    \
        [&entry_reflection, &key_field](const Protobuf::Message& a, const Protobuf::Message& b) {  \
          return entry_reflection->Get##get_type(a, key_field) <                                   \
                 entry_reflection->Get##get_type(b, key_field);                                    \
        });                                                                                        \
  } while (0)

#define HASH_FIXED(v)                                                                              \
  do {                                                                                             \
    auto q = v;                                                                                    \
    seed =                                                                                         \
        HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)}, seed); \
  } while (0)

#define HASH_STRING(v)                                                                             \
  do {                                                                                             \
    seed = HashUtil::xxHash64(v, seed);                                                            \
  } while (0)

uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed = 0);
uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor* field, uint64_t seed);

// To make a map serialize deterministically we need to force the order.
// Here we're going to sort the keys into numerical order for number keys,
// or lexicographical order for strings, and then hash them in that order.
uint64_t reflectionHashMapField(const Protobuf::Message& message,
                                const Protobuf::FieldDescriptor* field, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const auto reflection = message.GetReflection();
  auto entries = reflection->GetRepeatedFieldRef<Protobuf::Message>(message, field);
  std::vector<std::reference_wrapper<const Protobuf::Message>> map(entries.begin(), entries.end());
  auto entry_reflection = map.begin()->get().GetReflection();
  auto entry_descriptor = map.begin()->get().GetDescriptor();
  const FieldDescriptor* key_field = entry_descriptor->map_key();
  const FieldDescriptor* value_field = entry_descriptor->map_value();
  HASH_FIXED(key_field->number());
  switch (key_field->cpp_type()) {
  case FieldDescriptor::CPPTYPE_INT32:
    MAP_SORT_BY(Int32);
    break;
  case FieldDescriptor::CPPTYPE_UINT32:
    MAP_SORT_BY(UInt32);
    break;
  case FieldDescriptor::CPPTYPE_INT64:
    MAP_SORT_BY(Int64);
    break;
  case FieldDescriptor::CPPTYPE_UINT64:
    MAP_SORT_BY(UInt64);
    break;
  case FieldDescriptor::CPPTYPE_BOOL:
    MAP_SORT_BY(Bool);
    break;
  case FieldDescriptor::CPPTYPE_STRING: {
    std::sort(
        map.begin(), map.end(),
        [&entry_reflection, &key_field](const Protobuf::Message& a, const Protobuf::Message& b) {
          std::string scratch_a, scratch_b;
          return entry_reflection->GetStringReference(a, key_field, &scratch_a) <
                 entry_reflection->GetStringReference(b, key_field, &scratch_b);
        });
    break;
  }
  case FieldDescriptor::CPPTYPE_DOUBLE:
  case FieldDescriptor::CPPTYPE_FLOAT:
  case FieldDescriptor::CPPTYPE_ENUM:
  case FieldDescriptor::CPPTYPE_MESSAGE:
    IS_ENVOY_BUG("invalid map key type");
  }
  for (const auto& entry : map) {
    seed = reflectionHashField(entry, key_field, seed);
    seed = reflectionHashField(entry, value_field, seed);
  }
  return seed;
}

uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor* field, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const auto reflection = message.GetReflection();
  switch (field->cpp_type()) {
  case FieldDescriptor::CPPTYPE_INT32:
    REFLECTION_FOR_EACH(Int32, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_UINT32:
    REFLECTION_FOR_EACH(UInt32, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_INT64:
    REFLECTION_FOR_EACH(Int64, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_UINT64:
    REFLECTION_FOR_EACH(UInt64, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_DOUBLE:
    REFLECTION_FOR_EACH(Double, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_FLOAT:
    REFLECTION_FOR_EACH(Float, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_BOOL:
    REFLECTION_FOR_EACH(Bool, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_ENUM:
    REFLECTION_FOR_EACH(EnumValue, HASH_FIXED);
    break;
  case FieldDescriptor::CPPTYPE_STRING: {
    if (field->is_repeated()) {
      for (const std::string& str : reflection->GetRepeatedFieldRef<std::string>(message, field)) {
        HASH_STRING(str);
      }
    } else {
      std::string scratch;
      HASH_STRING(reflection->GetStringReference(message, field, &scratch));
    }
  } break;
  case FieldDescriptor::CPPTYPE_MESSAGE:
    if (field->is_map()) {
      seed = reflectionHashMapField(message, field, seed);
    } else if (field->is_repeated()) {
      for (const auto& submsg :
           reflection->GetRepeatedFieldRef<Protobuf::Message>(message, field)) {
        seed = reflectionHashMessage(submsg, seed);
      }
    } else {
      seed = reflectionHashMessage(reflection->GetMessage(message, field), seed);
    }
    break;
  }
  return seed;
}

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
  if (descriptor == nullptr) {
    return nullptr;
  }
  const Protobuf::Message* prototype =
      Protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
  auto msg = std::unique_ptr<Protobuf::Message>(prototype->New());
  any.UnpackTo(msg.get());
  return msg;
}

// This is intentionally ignoring unknown fields.
uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  std::string scratch;
  const auto reflection = message.GetReflection();
  const auto descriptor = message.GetDescriptor();
  if (descriptor->well_known_type() == Protobuf::Descriptor::WELLKNOWNTYPE_ANY) {
    const ProtobufWkt::Any* any = Protobuf::DynamicCastToGenerated<ProtobufWkt::Any>(&message);
    auto submsg = unpackAnyForReflection(*any);
    if (submsg == nullptr) {
      // If we wanted to handle unknown types in Any, this is where we'd have to do it.
      return seed;
    }
    HASH_STRING(any->type_url());
    return reflectionHashMessage(*submsg, seed);
  }
  std::vector<const FieldDescriptor*> fields;
  reflection->ListFields(message, &fields);
  // If we wanted to handle unknown fields, we'd need to also GetUnknownFields here.
  for (const auto field : fields) {
    seed = reflectionHashField(message, field, seed);
  }
  return seed;
}
} // namespace

uint64_t hash(const Protobuf::Message& message) {
  // Must use a nonzero initial seed, because the empty message must hash to nonzero.
  return reflectionHashMessage(message, 1);
}

} // namespace DeterministicProtoHash
} // namespace Envoy
#endif
