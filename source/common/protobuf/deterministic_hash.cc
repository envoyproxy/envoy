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

struct MapFieldSorter {
  MapFieldSorter(const Protobuf::RepeatedFieldRef<Protobuf::Message> entries)
      : entries_(entries.begin(), entries.end()),
        reflection_(*entries_.begin()->get().GetReflection()),
        descriptor_(*entries_.begin()->get().GetDescriptor()), key_field_(*descriptor_.map_key()),
        value_field_(*descriptor_.map_value()) {}
  const std::vector<std::reference_wrapper<const Protobuf::Message>>& sorted() {
    using Protobuf::FieldDescriptor;
    std::function<bool(const Protobuf::Message& a, const Protobuf::Message& b)> compareFn;
    switch (key_field_.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        return reflection_.GetInt32(a, &key_field_) < reflection_.GetInt32(b, &key_field_);
      };
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        return reflection_.GetUInt32(a, &key_field_) < reflection_.GetUInt32(b, &key_field_);
      };
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        return reflection_.GetInt64(a, &key_field_) < reflection_.GetInt64(b, &key_field_);
      };
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        return reflection_.GetUInt64(a, &key_field_) < reflection_.GetUInt64(b, &key_field_);
      };
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        return reflection_.GetBool(a, &key_field_) < reflection_.GetBool(b, &key_field_);
      };
      break;
    case FieldDescriptor::CPPTYPE_STRING: {
      compareFn = [this](const Protobuf::Message& a, const Protobuf::Message& b) {
        std::string scratch_a, scratch_b;
        return reflection_.GetStringReference(a, &key_field_, &scratch_a) <
               reflection_.GetStringReference(b, &key_field_, &scratch_b);
      };
      break;
    }
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_ENUM:
    case FieldDescriptor::CPPTYPE_MESSAGE:
      IS_ENVOY_BUG("invalid map key type");
    }
    std::sort(entries_.begin(), entries_.end(), compareFn);
    return entries_;
  }

  std::vector<std::reference_wrapper<const Protobuf::Message>> entries_;
  const Protobuf::Reflection& reflection_;
  const Protobuf::Descriptor& descriptor_;
  const Protobuf::FieldDescriptor& key_field_;
  const Protobuf::FieldDescriptor& value_field_;
};

// To make a map serialize deterministically we need to force the order.
// Here we're going to sort the keys into numerical order for number keys,
// or lexicographical order for strings, and then hash them in that order.
uint64_t reflectionHashMapField(const Protobuf::Message& message,
                                const Protobuf::FieldDescriptor* field, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const auto reflection = message.GetReflection();
  auto entries = reflection->GetRepeatedFieldRef<Protobuf::Message>(message, field);
  MapFieldSorter sorter(entries);
  auto& sorted_entries = sorter.sorted();
  for (const auto& entry : sorted_entries) {
    seed = reflectionHashField(entry, &sorter.key_field_, seed);
    seed = reflectionHashField(entry, &sorter.value_field_, seed);
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
    if (field->is_repeated()) {
      int c = reflection->FieldSize(message, field);
      for (int i = 0; i < c; i++) {
        int v = reflection->GetRepeatedEnumValue(message, field, i);
        seed = HashUtil::xxHash64(absl::string_view{reinterpret_cast<char*>(&v), sizeof(v)}, seed);
      }
    } else {
      int v = reflection->GetEnumValue(message, field);
      seed = HashUtil::xxHash64(absl::string_view{reinterpret_cast<char*>(&v), sizeof(v)}, seed);
    }
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
