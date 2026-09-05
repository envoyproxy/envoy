#include "source/common/json/proto_streamer.h"

#include <cmath>

#include "source/common/common/base64.h"
#include "source/common/common/macros.h"
#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor_helper.h"

namespace Envoy {
namespace Json {

namespace {

using Field = Protobuf::FieldDescriptor;

// Returns the value of `field`, from element `index` if the field is repeated.
// Pass a negative `index` to get the whole field.
#define REFLECTION_GET(Type, field, index)                                                         \
  (index < 0 ? reflection.Get##Type(message, &field)                                               \
             : reflection.GetRepeated##Type(message, &field, index))

// Whether ProtoJSON gives `message` a special representation rather than an object of its fields,
// which is every `well_known_type()` other than Any. Any is excluded, this streamer expands it into
// a frame.
//
// TODO(filipcacky): Struct, Value and ListValue should be streamed, see json_utility.cc
bool hasSpecialRepresentation(const Protobuf::Message& message) {
  switch (message.GetDescriptor()->well_known_type()) {
  case Protobuf::Descriptor::WELLKNOWNTYPE_UNSPECIFIED:
  case Protobuf::Descriptor::WELLKNOWNTYPE_ANY:
    return false;
  default:
    return true;
  }
}

constexpr absl::string_view RedactedText = "[redacted]";

const std::string& redactedBase64() {
  CONSTRUCT_ON_FIRST_USE(std::string, Base64::encode(RedactedText));
}

// Whether redaction clears `field` instead of replacing it, which it does to everything but
// messages and text.
bool redactionClears(const Field& field) {
  return field.cpp_type() != Field::CPPTYPE_MESSAGE && field.type() != Field::TYPE_STRING &&
         field.type() != Field::TYPE_BYTES;
}

ProtobufTypes::MessagePtr redactedCopy(const Protobuf::Message& message,
                                       bool ancestor_is_sensitive) {
  ProtobufTypes::MessagePtr copy(message.New());
  copy->CopyFrom(message);
  if (ancestor_is_sensitive) {
    MessageUtil::redactAll(*copy);
  } else {
    MessageUtil::redact(*copy);
  }
  return copy;
}

bool isTypedStruct(const Protobuf::Descriptor& descriptor) {
  const absl::string_view name = descriptor.full_name();
  return name == "xds.type.v3.TypedStruct" || name == "udpa.type.v1.TypedStruct";
}

// Render map key 'field's value as a string.
absl::string_view mapKeyToString(const Protobuf::Message& entry, const Field& field,
                                 std::string& scratch) {
  const Protobuf::Reflection& reflection = *entry.GetReflection();
  switch (field.cpp_type()) {
  case Field::CPPTYPE_BOOL:
    return reflection.GetBool(entry, &field) ? "true" : "false";
  case Field::CPPTYPE_INT32:
    scratch.clear();
    absl::StrAppend(&scratch, reflection.GetInt32(entry, &field));
    return scratch;
  case Field::CPPTYPE_INT64:
    scratch.clear();
    absl::StrAppend(&scratch, reflection.GetInt64(entry, &field));
    return scratch;
  case Field::CPPTYPE_UINT32:
    scratch.clear();
    absl::StrAppend(&scratch, reflection.GetUInt32(entry, &field));
    return scratch;
  case Field::CPPTYPE_UINT64:
    scratch.clear();
    absl::StrAppend(&scratch, reflection.GetUInt64(entry, &field));
    return scratch;
  default:
    return reflection.GetStringReference(entry, &field, &scratch);
  }
}

} // namespace

MessageStreamer::MessageStreamer(const Protobuf::Message& message, BufferStreamer::Level& level,
                                 TypeUrl type_url, FieldNames field_names, Sensitive sensitive)
    : json_names_(field_names == FieldNames::LowerCamelCase),
      redact_(sensitive == Sensitive::Redact) {
  const std::string name =
      type_url == TypeUrl::Emit
          ? TypeUtil::descriptorFullNameToTypeUrl(message.GetDescriptor()->full_name())
          : "";
  emitNamedMessage(message, level, name, false);
}

void MessageStreamer::emitNamedMessage(const Protobuf::Message& message,
                                       BufferStreamer::Level& level, absl::string_view type_url,
                                       bool is_sensitive, ProtobufTypes::MessagePtr owned) {
  // ProtoJSON pairs a special representation with its `@type` under `value`.
  if (!type_url.empty() && hasSpecialRepresentation(message)) {
    BufferStreamer::MapPtr map = level.addMap();
    map->addKey("@type");
    map->addString(type_url);
    map->addKey("value");
    emitSpecialRepresentation(message, *map, is_sensitive);
    return;
  }

  Frame& frame = owned == nullptr ? pushFrame(message, level, is_sensitive)
                                  : pushOwnedFrame(std::move(owned), level, is_sensitive);
  if (!type_url.empty()) {
    frame.map_->addKey("@type");
    frame.map_->addString(type_url);
  }
}

MessageStreamer::~MessageStreamer() {
  while (!stack_.empty()) {
    stack_.pop_back();
  }
}

bool MessageStreamer::next() {
  if (stack_.empty()) {
    return false;
  }

  Frame& frame = stack_.back();
  if (frame.elements_ != nullptr) {
    nextElement(frame);
    return true;
  }

  if (frame.next_field_ >= frame.fields_.size()) {
    stack_.pop_back();
    return !stack_.empty();
  }

  startField(frame);
  return true;
}

void MessageStreamer::nextElement(Frame& frame) {
  const Protobuf::Reflection& reflection = *frame.message_.GetReflection();
  const Field& field = *frame.fields_[frame.next_field_];

  if (frame.next_element_ >= reflection.FieldSize(frame.message_, &field)) {
    frame.elements_.reset();
    frame.next_element_ = 0;
    ++frame.next_field_;
    return;
  }

  const int index = frame.next_element_++;
  if (!field.is_map()) {
    emitValue(frame.message_, field, index, *frame.elements_, frame.field_is_sensitive_);
    return;
  }

  // A repeated field's level is always BufferStreamer::Array, unless it is a map,
  // in which case it is BufferStreamer::Map, so the static_cast is safe.
  // The level is created in MessageStreamer::startField.
  BufferStreamer::Map& entries = static_cast<BufferStreamer::Map&>(*frame.elements_);
  const Protobuf::Message& entry = reflection.GetRepeatedMessage(frame.message_, &field, index);
  // Keys are left alone, redacting them would collapse the map onto one key.
  entries.addKey(mapKeyToString(entry, *field.message_type()->map_key(), scratch_));
  emitValue(entry, *field.message_type()->map_value(), -1, entries, frame.field_is_sensitive_);
}

void MessageStreamer::startField(Frame& frame) {
  const Field& field = *frame.fields_[frame.next_field_];

  frame.field_is_sensitive_ =
      redact_ && (frame.ancestor_is_sensitive_ || MessageUtil::isSensitiveField(field));
  // A cleared field is unset, so it drops out of the output instead of printing a default.
  if (frame.field_is_sensitive_ && redactionClears(field)) {
    ++frame.next_field_;
    return;
  }

  frame.map_->addKey(json_names_ ? field.json_name() : field.name());
  // is_map implies is_repeated, so handle it first.
  // https://protobuf.dev/programming-guides/proto3/#backwards
  if (field.is_map()) {
    frame.elements_ = frame.map_->addMap();
    return;
  }
  if (field.is_repeated()) {
    frame.elements_ = frame.map_->addArray();
    return;
  }
  ++frame.next_field_;
  emitValue(frame.message_, field, -1, *frame.map_, frame.field_is_sensitive_);
}

void MessageStreamer::emitValue(const Protobuf::Message& message, const Field& field, int index,
                                BufferStreamer::Level& level, bool is_sensitive) {
  const Protobuf::Reflection& reflection = *message.GetReflection();
  // A map entry and a wrapper print their value even when unset, and startField cannot drop either,
  // it never sees the value field. Redaction clears it, so the default comes off an empty message.
  if (is_sensitive && redactionClears(field)) {
    const ProtobufTypes::MessagePtr cleared(message.New());
    emitValue(*cleared, field, -1, level, false);
    return;
  }
  switch (field.cpp_type()) {
  case Field::CPPTYPE_INT32:
    level.addNumber(static_cast<int64_t>(REFLECTION_GET(Int32, field, index)));
    return;
  case Field::CPPTYPE_UINT32:
    level.addNumber(static_cast<uint64_t>(REFLECTION_GET(UInt32, field, index)));
    return;
  case Field::CPPTYPE_INT64:
    // ProtoJSON spells 64 bit integers as decimal strings.
    // https://protobuf.dev/programming-guides/json/#int64-strings
    scratch_.clear();
    absl::StrAppend(&scratch_, REFLECTION_GET(Int64, field, index));
    level.addString(scratch_);
    return;
  case Field::CPPTYPE_UINT64:
    scratch_.clear();
    absl::StrAppend(&scratch_, REFLECTION_GET(UInt64, field, index));
    level.addString(scratch_);
    return;
  case Field::CPPTYPE_BOOL:
    level.addBool(REFLECTION_GET(Bool, field, index));
    return;
  case Field::CPPTYPE_DOUBLE:
  case Field::CPPTYPE_FLOAT: {
    const double number = field.cpp_type() == Field::CPPTYPE_DOUBLE
                              ? REFLECTION_GET(Double, field, index)
                              : REFLECTION_GET(Float, field, index);
    if (std::isfinite(number)) {
      level.addNumber(number);
    } else if (std::isnan(number)) {
      level.addString("NaN");
    } else {
      level.addString(number > 0 ? "Infinity" : "-Infinity");
    }
    return;
  }
  case Field::CPPTYPE_ENUM: {
    // An enum is its name, unless it holds a number we have no name for.
    const int number = REFLECTION_GET(EnumValue, field, index);
    const Protobuf::EnumValueDescriptor* value = field.enum_type()->FindValueByNumber(number);
    if (value == nullptr) {
      level.addNumber(static_cast<int64_t>(number));
    } else {
      level.addString(value->name());
    }
    return;
  }
  case Field::CPPTYPE_STRING: {
    if (is_sensitive) {
      level.addString(field.type() == Field::TYPE_BYTES ? absl::string_view(redactedBase64())
                                                        : RedactedText);
      return;
    }
    scratch_.clear();
    const std::string& value =
        index < 0 ? reflection.GetStringReference(message, &field, &scratch_)
                  : reflection.GetRepeatedStringReference(message, &field, index, &scratch_);
    // ProtoJSON spells bytes as base64.
    level.addString(field.type() == Field::TYPE_BYTES ? Base64::encode(value) : value);
    return;
  }
  case Field::CPPTYPE_MESSAGE:
    emitMessage(REFLECTION_GET(Message, field, index), level, is_sensitive);
    return;
  }
}

void MessageStreamer::emitMessage(const Protobuf::Message& message, BufferStreamer::Level& level,
                                  bool is_sensitive) {
  const Protobuf::Descriptor& descriptor = *message.GetDescriptor();
  switch (descriptor.well_known_type()) {
  case Protobuf::Descriptor::WELLKNOWNTYPE_UNSPECIFIED:
    if (redact_ && isTypedStruct(descriptor)) {
      pushOwnedFrame(redactedCopy(message, is_sensitive), level, false);
      return;
    }
    pushFrame(message, level, is_sensitive);
    return;
  case Protobuf::Descriptor::WELLKNOWNTYPE_ANY:
    emitAny(message, level, is_sensitive);
    return;
  case Protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
  case Protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
    // A wrapper is spelled as the value of its only field.
    emitValue(message, *descriptor.field(0), -1, level, is_sensitive);
    return;
  case Protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
    if (!is_sensitive) {
      if (const auto* duration = Protobuf::DynamicCastMessage<Protobuf::Duration>(&message)) {
        level.addString(Protobuf::util::TimeUtil::ToString(*duration));
        return;
      }
    }
    emitSpecialRepresentation(message, level, is_sensitive);
    return;
  case Protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
    if (!is_sensitive) {
      if (const auto* timestamp = Protobuf::DynamicCastMessage<Protobuf::Timestamp>(&message)) {
        level.addString(Protobuf::util::TimeUtil::ToString(*timestamp));
        return;
      }
    }
    emitSpecialRepresentation(message, level, is_sensitive);
    return;
  default:
    emitSpecialRepresentation(message, level, is_sensitive);
    return;
  }
}

void MessageStreamer::emitAny(const Protobuf::Message& message, BufferStreamer::Level& level,
                              bool is_sensitive) {
  const Protobuf::Any* any = Protobuf::DynamicCastMessage<Protobuf::Any>(&message);
  Protobuf::Any copy;
  if (any == nullptr) {
    copy.CopyFrom(message);
    any = &copy;
  }

  ProtobufTypes::MessagePtr packed = ProtobufMessage::Helper::typeUrlToMessage(any->type_url());
  if (packed == nullptr || !MessageUtil::unpackTo(*any, *packed).ok()) {
    BufferStreamer::MapPtr map = level.addMap();
    map->addKey("@type");
    map->addString(any->type_url());
    return;
  }

  const Protobuf::Message& payload = *packed;
  emitNamedMessage(payload, level, any->type_url(), is_sensitive, std::move(packed));
}

void MessageStreamer::emitSpecialRepresentation(const Protobuf::Message& message,
                                                BufferStreamer::Level& level, bool is_sensitive) {
  // Protobuf prints the whole message, so it has to be handed a redacted copy.
  const ProtobufTypes::MessagePtr redacted = is_sensitive ? redactedCopy(message, true) : nullptr;
  const absl::StatusOr<std::string> json =
      MessageUtil::getJsonStringFromMessage(redacted == nullptr ? message : *redacted);
  if (json.ok()) {
    level.addRawJson(*json);
  } else {
    level.addNull();
  }
}

MessageStreamer::Frame& MessageStreamer::pushFrame(const Protobuf::Message& message,
                                                   BufferStreamer::Level& level,
                                                   bool ancestor_is_sensitive) {
  Frame& frame = stack_.emplace_back(message, level.addMap());
  frame.ancestor_is_sensitive_ = ancestor_is_sensitive;
  return frame;
}

MessageStreamer::Frame& MessageStreamer::pushOwnedFrame(ProtobufTypes::MessagePtr message,
                                                        BufferStreamer::Level& level,
                                                        bool ancestor_is_sensitive) {
  Frame& frame = pushFrame(*message, level, ancestor_is_sensitive);
  frame.owned_ = std::move(message);
  return frame;
}

#undef REFLECTION_GET

} // namespace Json
} // namespace Envoy
