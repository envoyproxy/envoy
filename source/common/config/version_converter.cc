#include "common/config/version_converter.h"

#include "common/common/assert.h"
#include "common/config/api_type_oracle.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Config {

namespace {

const char DeprecatedFieldShadowPrefix[] = "hidden_envoy_deprecated_";

// TODO(htuch): refactor these message visitor patterns into utility.cc and share with
// MessageUtil::checkForUnexpectedFields.
void messageFieldVisitor(std::function<void(Protobuf::Message&, const Protobuf::FieldDescriptor&,
                                            const Protobuf::Reflection&)>
                             field_fn,
                         Protobuf::Message& message) {
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    field_fn(message, *field, *reflection);
    // If this is a message, recurse to scrub deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          messageFieldVisitor(field_fn, *reflection->MutableRepeatedMessage(&message, field, j));
        }
      } else {
        messageFieldVisitor(field_fn, *reflection->MutableMessage(&message, field));
      }
    }
  }
}

void constMessageFieldVisitor(
    std::function<void(const Protobuf::Message&, const Protobuf::FieldDescriptor&,
                       const Protobuf::Reflection&)>
        field_fn,
    const Protobuf::Message& message) {
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    field_fn(message, *field, *reflection);
    // If this is a message, recurse to scrub deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          constMessageFieldVisitor(field_fn, reflection->GetRepeatedMessage(message, field, j));
        }
      } else {
        constMessageFieldVisitor(field_fn, reflection->GetMessage(message, field));
      }
    }
  }
}

// Reinterpret a Protobuf message as another Protobuf message by converting to
// wire format and back. This only works for messages that can be effectively
// duck typed this way, e.g. with a subtype relationship modulo field name.
void wireCast(const Protobuf::Message& src, Protobuf::Message& dst) {
  dst.ParseFromString(src.SerializeAsString());
}

} // namespace

void VersionConverter::upgrade(const Protobuf::Message& prev_message,
                               Protobuf::Message& next_message) {
  wireCast(prev_message, next_message);
}

DynamicMessagePtr VersionConverter::downgrade(const Protobuf::Message& message) {
  auto downgraded_message = std::make_unique<DynamicMessage>();
  const Protobuf::Descriptor* prev_desc = ApiTypeOracle::getEarlierVersionDescriptor(message);
  if (prev_desc != nullptr) {
    downgraded_message->msg_.reset(
        downgraded_message->dynamic_msg_factory_.GetPrototype(prev_desc)->New());
    wireCast(message, *downgraded_message->msg_);
    return downgraded_message;
  }
  // Unnecessary copy, since the existing message is being treated as
  // "downgraded". However, we want to transfer an owned object, so this is the
  // best we can do.
  const Protobuf::Descriptor* desc = message.GetDescriptor();
  downgraded_message->msg_.reset(
      downgraded_message->dynamic_msg_factory_.GetPrototype(desc)->New());
  downgraded_message->msg_->MergeFrom(message);
  return downgraded_message;
}

DynamicMessagePtr
VersionConverter::reinterpret(const Protobuf::Message& message,
                              envoy::config::core::v3alpha::ApiVersion api_version) {
  switch (api_version) {
  case envoy::config::core::v3alpha::ApiVersion::AUTO:
  case envoy::config::core::v3alpha::ApiVersion::V2:
    // TODO(htuch): this works as long as there are no new fields in the v3+
    // DiscoveryRequest. When they are added, we need to do a full v2 conversion
    // and also discard unknown fields. Tracked at
    // https://github.com/envoyproxy/envoy/issues/9619.
    return downgrade(message);
  case envoy::config::core::v3alpha::ApiVersion::V3ALPHA: {
    // We need to scrub the hidden fields.
    auto non_shadowed_message = std::make_unique<DynamicMessage>();
    const Protobuf::Descriptor* desc = message.GetDescriptor();
    non_shadowed_message->msg_.reset(
        non_shadowed_message->dynamic_msg_factory_.GetPrototype(desc)->New());
    non_shadowed_message->msg_->MergeFrom(message);
    VersionUtil::scrubHiddenEnvoyDeprecated(*non_shadowed_message->msg_);
    return non_shadowed_message;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool VersionUtil::hasHiddenEnvoyDeprecated(const Protobuf::Message& message) {
  bool has_hidden = false;
  constMessageFieldVisitor(
      [&has_hidden](const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                    const Protobuf::Reflection& reflection) {
        if (absl::StartsWith(field.name(), DeprecatedFieldShadowPrefix)) {
          if (field.is_repeated()) {
            has_hidden |= reflection.FieldSize(message, &field) > 0;
          } else {
            has_hidden |= reflection.HasField(message, &field);
          }
        }
      },
      message);
  return has_hidden;
}

void VersionUtil::scrubHiddenEnvoyDeprecated(Protobuf::Message& message) {
  messageFieldVisitor(
      [](Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
         const Protobuf::Reflection& reflection) {
        if (absl::StartsWith(field.name(), DeprecatedFieldShadowPrefix)) {
          reflection.ClearField(&message, &field);
        }
      },
      message);
}

} // namespace Config
} // namespace Envoy
