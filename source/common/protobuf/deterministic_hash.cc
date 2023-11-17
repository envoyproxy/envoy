#if defined(ENVOY_ENABLE_FULL_PROTOS)
#include "source/common/protobuf/deterministic_hash.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"

namespace Envoy {
namespace DeterministicProtoHash {
namespace {

#define REFLECTION_FOR_EACH(get_type, value_type)                                                  \
  if (field->is_repeated()) {                                                                      \
    for (const value_type& q : reflection->GetRepeatedFieldRef<value_type>(message, field)) {      \
      seed = HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)},   \
                                seed);                                                             \
    }                                                                                              \
  } else {                                                                                         \
    const value_type q = reflection->Get##get_type(message, field);                                \
    seed =                                                                                         \
        HashUtil::xxHash64(absl::string_view{reinterpret_cast<const char*>(&q), sizeof(q)}, seed); \
  }

uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed = 0);
uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor* field, uint64_t seed);

// MapFieldComparator uses selectCompareFn at init-time, rather than putting the
// switch into the comparator, because that way the switch is only performed once,
// rather than O(n*log(n)) times during std::sort.
//
// This uses a member-function pointer rather than a virtual function with a
// polymorphic object selected during a create function because std::sort
// does not accept a polymorphic object as a comparator (due to that argument
// being taken by value).
struct MapFieldComparator {
  MapFieldComparator(const Protobuf::Message& first_msg)
      : reflection_(*first_msg.GetReflection()), descriptor_(*first_msg.GetDescriptor()),
        key_field_(*descriptor_.map_key()), compare_fn_(selectCompareFn()) {}
  bool operator()(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return (this->*compare_fn_)(a, b);
  }

private:
  bool compareByInt32(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return reflection_.GetInt32(a, &key_field_) < reflection_.GetInt32(b, &key_field_);
  }
  bool compareByUInt32(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return reflection_.GetUInt32(a, &key_field_) < reflection_.GetUInt32(b, &key_field_);
  }
  bool compareByInt64(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return reflection_.GetInt64(a, &key_field_) < reflection_.GetInt64(b, &key_field_);
  }
  bool compareByUInt64(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return reflection_.GetUInt64(a, &key_field_) < reflection_.GetUInt64(b, &key_field_);
  }
  bool compareByBool(const Protobuf::Message& a, const Protobuf::Message& b) const {
    return reflection_.GetBool(a, &key_field_) < reflection_.GetBool(b, &key_field_);
  }
  bool compareByString(const Protobuf::Message& a, const Protobuf::Message& b) const {
    std::string scratch_a, scratch_b;
    return reflection_.GetStringReference(a, &key_field_, &scratch_a) <
           reflection_.GetStringReference(b, &key_field_, &scratch_b);
  }
  using CompareMemberFn = bool (MapFieldComparator::*)(const Protobuf::Message& a,
                                                       const Protobuf::Message& b) const;
  CompareMemberFn selectCompareFn() {
    using Protobuf::FieldDescriptor;
    switch (key_field_.cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      return &MapFieldComparator::compareByInt32;
    case FieldDescriptor::CPPTYPE_UINT32:
      return &MapFieldComparator::compareByUInt32;
    case FieldDescriptor::CPPTYPE_INT64:
      return &MapFieldComparator::compareByInt64;
    case FieldDescriptor::CPPTYPE_UINT64:
      return &MapFieldComparator::compareByUInt64;
    case FieldDescriptor::CPPTYPE_BOOL:
      return &MapFieldComparator::compareByBool;
    case FieldDescriptor::CPPTYPE_STRING:
      return &MapFieldComparator::compareByString;
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_ENUM:
    case FieldDescriptor::CPPTYPE_MESSAGE:
      break;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
  const Protobuf::Reflection& reflection_;
  const Protobuf::Descriptor& descriptor_;
  const Protobuf::FieldDescriptor& key_field_;
  CompareMemberFn compare_fn_;
};

std::vector<std::reference_wrapper<const Protobuf::Message>>
sortedMapField(const Protobuf::RepeatedFieldRef<Protobuf::Message> map_entries) {
  std::vector<std::reference_wrapper<const Protobuf::Message>> entries(map_entries.begin(),
                                                                       map_entries.end());
  MapFieldComparator compare(*entries.begin());
  std::sort(entries.begin(), entries.end(), compare);
  return entries;
}

// To make a map serialize deterministically we need to force the order.
// Here we're going to sort the keys into numerical order for number keys,
// or lexicographical order for strings, and then hash them in that order.
uint64_t reflectionHashMapField(const Protobuf::Message& message,
                                const Protobuf::FieldDescriptor* field, uint64_t seed) {
  const Protobuf::Reflection* reflection = message.GetReflection();
  auto sorted_entries =
      sortedMapField(reflection->GetRepeatedFieldRef<Protobuf::Message>(message, field));
  const Protobuf::Descriptor* map_descriptor = sorted_entries.begin()->get().GetDescriptor();
  const Protobuf::FieldDescriptor* key_field = map_descriptor->map_key();
  const Protobuf::FieldDescriptor* value_field = map_descriptor->map_value();
  for (const Protobuf::Message& entry : sorted_entries) {
    seed = reflectionHashField(entry, key_field, seed);
    seed = reflectionHashField(entry, value_field, seed);
  }
  return seed;
}

uint64_t reflectionHashField(const Protobuf::Message& message,
                             const Protobuf::FieldDescriptor* field, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  const Protobuf::Reflection* reflection = message.GetReflection();
  {
    const int q = field->number();
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
      for (const Protobuf::Message& submsg :
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
  std::unique_ptr<Protobuf::Message> msg(prototype->New());
  any.UnpackTo(msg.get());
  return msg;
}

// This is intentionally ignoring unknown fields.
uint64_t reflectionHashMessage(const Protobuf::Message& message, uint64_t seed) {
  using Protobuf::FieldDescriptor;
  std::string scratch;
  const Protobuf::Reflection* reflection = message.GetReflection();
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  seed = HashUtil::xxHash64(descriptor->full_name(), seed);
  if (descriptor->well_known_type() == Protobuf::Descriptor::WELLKNOWNTYPE_ANY) {
    const ProtobufWkt::Any* any = Protobuf::DynamicCastToGenerated<ProtobufWkt::Any>(&message);
    std::unique_ptr<Protobuf::Message> submsg = unpackAnyForReflection(*any);
    if (submsg == nullptr) {
      // If we wanted to handle unknown types in Any, this is where we'd have to do it.
      return seed;
    }
    return reflectionHashMessage(*submsg, seed);
  }
  std::vector<const FieldDescriptor*> fields;
  reflection->ListFields(message, &fields);
  // If we wanted to handle unknown fields, we'd need to also GetUnknownFields here.
  for (const FieldDescriptor* field : fields) {
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
