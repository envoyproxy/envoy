#if defined(ENVOY_ENABLE_FULL_PROTOS)
#include "source/common/protobuf/deterministic_hash.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"

namespace Envoy {
namespace DeterministicProtoHash {
namespace {

#define REFLECTION_FOR_EACH(get_type, value_type)                                                  \
  if (field->is_repeated()) {                                                                      \
    for (const auto q : reflection->GetRepeatedFieldRef<value_type>(message, field)) {             \
      seed = HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)},   \
                                seed);                                                             \
    }                                                                                              \
  } else {                                                                                         \
    const auto q = reflection->Get##get_type(message, field);                                      \
    seed =                                                                                         \
        HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)}, seed); \
  }

uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed = 0);
uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor* field, uint64_t seed);

template <typename T>
std::function<bool(const Protobuf::Message& a, const Protobuf::Message& b)>
makeCompareFn(std::function<T(const Protobuf::Message& msg)> getter) {
  return [getter](const Protobuf::Message& a, const Protobuf::Message& b) {
    return getter(a) < getter(b);
  };
}

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
  std::function<bool(const Protobuf::Message& a, const Protobuf::Message& b)> compareFn;
  switch (key_field->cpp_type()) {
  case FieldDescriptor::CPPTYPE_INT32:
    compareFn =
        makeCompareFn<int32_t>([&entry_reflection, &key_field](const Protobuf::Message& msg) {
          return entry_reflection->GetInt32(msg, key_field);
        });
    break;
  case FieldDescriptor::CPPTYPE_UINT32:
    compareFn =
        makeCompareFn<uint32_t>([&entry_reflection, &key_field](const Protobuf::Message& msg) {
          return entry_reflection->GetUInt32(msg, key_field);
        });
    break;
  case FieldDescriptor::CPPTYPE_INT64:
    compareFn =
        makeCompareFn<int64_t>([&entry_reflection, &key_field](const Protobuf::Message& msg) {
          return entry_reflection->GetInt64(msg, key_field);
        });
    break;
  case FieldDescriptor::CPPTYPE_UINT64:
    compareFn =
        makeCompareFn<uint64_t>([&entry_reflection, &key_field](const Protobuf::Message& msg) {
          return entry_reflection->GetUInt64(msg, key_field);
        });
    break;
  case FieldDescriptor::CPPTYPE_BOOL:
    compareFn = makeCompareFn<bool>([&entry_reflection, &key_field](const Protobuf::Message& msg) {
      return entry_reflection->GetBool(msg, key_field);
    });
    break;
  case FieldDescriptor::CPPTYPE_STRING: {
    compareFn = [&entry_reflection, &key_field](const Protobuf::Message& a,
                                                const Protobuf::Message& b) {
      std::string scratch_a, scratch_b;
      return entry_reflection->GetStringReference(a, key_field, &scratch_a) <
             entry_reflection->GetStringReference(b, key_field, &scratch_b);
    };
    break;
  }
  case FieldDescriptor::CPPTYPE_DOUBLE:
  case FieldDescriptor::CPPTYPE_FLOAT:
  case FieldDescriptor::CPPTYPE_ENUM:
  case FieldDescriptor::CPPTYPE_MESSAGE:
    IS_ENVOY_BUG("invalid map key type");
  }
  std::sort(map.begin(), map.end(), compareFn);
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
  {
    const auto q = field->number();
    seed =
        HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)}, seed);
  }
  switch (field->cpp_type()) {
  case FieldDescriptor::CPPTYPE_INT32:
    REFLECTION_FOR_EACH(Int32, int32_t);
    break;
  case FieldDescriptor::CPPTYPE_UINT32:
    REFLECTION_FOR_EACH(UInt32, uint32_t);
    break;
  case FieldDescriptor::CPPTYPE_INT64:
    REFLECTION_FOR_EACH(Int64, int64_t);
    break;
  case FieldDescriptor::CPPTYPE_UINT64:
    REFLECTION_FOR_EACH(UInt64, uint64_t);
    break;
  case FieldDescriptor::CPPTYPE_DOUBLE:
    REFLECTION_FOR_EACH(Double, double);
    break;
  case FieldDescriptor::CPPTYPE_FLOAT:
    REFLECTION_FOR_EACH(Float, float);
    break;
  case FieldDescriptor::CPPTYPE_BOOL:
    REFLECTION_FOR_EACH(Bool, bool);
    break;
  case FieldDescriptor::CPPTYPE_ENUM:
    REFLECTION_FOR_EACH(EnumValue, uint32_t);
    break;
  case FieldDescriptor::CPPTYPE_STRING: {
    if (field->is_repeated()) {
      for (const std::string& str : reflection->GetRepeatedFieldRef<std::string>(message, field)) {
        seed = HashUtil::xxHash64(str, seed);
      }
    } else {
      std::string scratch;
      seed = HashUtil::xxHash64(reflection->GetStringReference(message, field, &scratch), seed);
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
  seed = HashUtil::xxHash64(descriptor->full_name(), seed);
  if (descriptor->well_known_type() == Protobuf::Descriptor::WELLKNOWNTYPE_ANY) {
    const ProtobufWkt::Any* any = Protobuf::DynamicCastToGenerated<ProtobufWkt::Any>(&message);
    auto submsg = unpackAnyForReflection(*any);
    if (submsg == nullptr) {
      // If we wanted to handle unknown types in Any, this is where we'd have to do it.
      return seed;
    }
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

uint64_t hash(const Protobuf::Message& message) { return reflectionHashMessage(message, 0); }

} // namespace DeterministicProtoHash
} // namespace Envoy
#endif
