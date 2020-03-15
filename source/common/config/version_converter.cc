#include "common/config/version_converter.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/config/api_type_oracle.h"
#include "common/protobuf/visitor.h"
#include "common/protobuf/well_known.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Config {

namespace {

const char DeprecatedFieldShadowPrefix[] = "hidden_envoy_deprecated_";

class ProtoVisitor {
public:
  virtual ~ProtoVisitor() = default;

  // Invoked when a field is visited, with the message, field descriptor and
  // context. Returns a new context for use when traversing the sub-message in a
  // field.
  virtual const void* onField(Protobuf::Message&, const Protobuf::FieldDescriptor&,
                              const void* ctxt) {
    return ctxt;
  }

  // Invoked when a message is visited, with the message and a context.
  virtual void onMessage(Protobuf::Message&, const void*){};
};

// Reinterpret a Protobuf message as another Protobuf message by converting to wire format and back.
// This only works for messages that can be effectively duck typed this way, e.g. with a subtype
// relationship modulo field name.
void wireCast(const Protobuf::Message& src, Protobuf::Message& dst) {
  // This should should generally succeed, but if there are malformed UTF-8 strings in a message,
  // this can fail.
  if (!dst.ParseFromString(src.SerializeAsString())) {
    throw EnvoyException("Unable to deserialize during wireCast()");
  }
}

// Create a new dynamic message based on some message wire cast to the target
// descriptor. If the descriptor is null, a copy is performed.
DynamicMessagePtr createForDescriptorWithCast(const Protobuf::Message& message,
                                              const Protobuf::Descriptor* desc) {
  auto dynamic_message = std::make_unique<DynamicMessage>();
  if (desc != nullptr) {
    dynamic_message->msg_.reset(dynamic_message->dynamic_msg_factory_.GetPrototype(desc)->New());
    wireCast(message, *dynamic_message->msg_);
    return dynamic_message;
  }
  // Unnecessary copy, since the existing message is being treated as
  // "dynamic". However, we want to transfer an owned object, so this is the
  // best we can do.
  dynamic_message->msg_.reset(message.New());
  dynamic_message->msg_->MergeFrom(message);
  return dynamic_message;
}

// This needs to be recursive, since sub-messages are consumed and stored
// internally, we later want to recover their original types.
void annotateWithOriginalType(const Protobuf::Descriptor& prev_descriptor,
                              Protobuf::Message& next_message) {
  class TypeAnnotatingProtoVisitor : public ProtobufMessage::ProtoVisitor {
  public:
    void onMessage(Protobuf::Message& message, const void* ctxt) override {
      const Protobuf::Descriptor* descriptor = message.GetDescriptor();
      const Protobuf::Reflection* reflection = message.GetReflection();
      const Protobuf::Descriptor& prev_descriptor = *static_cast<const Protobuf::Descriptor*>(ctxt);
      // If they are the same type, there's no possibility of any different type
      // further down, so we're done.
      if (descriptor->full_name() == prev_descriptor.full_name()) {
        return;
      }
      auto* unknown_field_set = reflection->MutableUnknownFields(&message);
      unknown_field_set->AddLengthDelimited(ProtobufWellKnown::OriginalTypeFieldNumber,
                                            prev_descriptor.full_name());
    }

    const void* onField(Protobuf::Message&, const Protobuf::FieldDescriptor& field,
                        const void* ctxt) override {
      const Protobuf::Descriptor& prev_descriptor = *static_cast<const Protobuf::Descriptor*>(ctxt);
      // TODO(htuch): This is a terrible hack, there should be no per-resource
      // business logic in this file. The reason this is required is that
      // endpoints, when captured in configuration such as inlined hosts in
      // Clusters for config dump purposes, can potentially contribute a
      // significant amount to memory consumption. stats_integration_test
      // complains as a result if we increase any memory due to type annotations.
      // In theory, we should be able to just clean up these annotations in
      // ClusterManagerImpl with type erasure, but protobuf doesn't free up memory
      // as expected, we probably need some arena level trick to address this.
      if (prev_descriptor.full_name() == "envoy.api.v2.Cluster" &&
          (field.name() == "hidden_envoy_deprecated_hosts" || field.name() == "load_assignment")) {
        // This will cause the sub-message visit to abort early.
        return field.message_type();
      }
      const Protobuf::FieldDescriptor* prev_field =
          prev_descriptor.FindFieldByNumber(field.number());
      return prev_field->message_type();
    }
  };
  TypeAnnotatingProtoVisitor proto_visitor;
  ProtobufMessage::traverseMutableMessage(proto_visitor, next_message, &prev_descriptor);
}

} // namespace

void VersionConverter::upgrade(const Protobuf::Message& prev_message,
                               Protobuf::Message& next_message) {
  wireCast(prev_message, next_message);
  // Track original type to support recoverOriginal().
  annotateWithOriginalType(*prev_message.GetDescriptor(), next_message);
}

void VersionConverter::eraseOriginalTypeInformation(Protobuf::Message& message) {
  class TypeErasingProtoVisitor : public ProtobufMessage::ProtoVisitor {
  public:
    void onMessage(Protobuf::Message& message, const void*) override {
      const Protobuf::Reflection* reflection = message.GetReflection();
      auto* unknown_field_set = reflection->MutableUnknownFields(&message);
      unknown_field_set->DeleteByNumber(ProtobufWellKnown::OriginalTypeFieldNumber);
    }
  };
  TypeErasingProtoVisitor proto_visitor;
  ProtobufMessage::traverseMutableMessage(proto_visitor, message, nullptr);
}

DynamicMessagePtr VersionConverter::recoverOriginal(const Protobuf::Message& upgraded_message) {
  const Protobuf::Reflection* reflection = upgraded_message.GetReflection();
  const auto& unknown_field_set = reflection->GetUnknownFields(upgraded_message);
  for (int i = 0; i < unknown_field_set.field_count(); ++i) {
    const auto& unknown_field = unknown_field_set.field(i);
    if (unknown_field.number() == ProtobufWellKnown::OriginalTypeFieldNumber) {
      ASSERT(unknown_field.type() == Protobuf::UnknownField::TYPE_LENGTH_DELIMITED);
      const std::string& original_type = unknown_field.length_delimited();
      const Protobuf::Descriptor* original_descriptor =
          Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(original_type);
      auto result = createForDescriptorWithCast(upgraded_message, original_descriptor);
      // We should clear out the OriginalTypeFieldNumber in the recovered message.
      eraseOriginalTypeInformation(*result->msg_);
      return result;
    }
  }
  return createForDescriptorWithCast(upgraded_message, nullptr);
}

DynamicMessagePtr VersionConverter::downgrade(const Protobuf::Message& message) {
  const Protobuf::Descriptor* prev_desc =
      ApiTypeOracle::getEarlierVersionDescriptor(message.GetDescriptor()->full_name());
  return createForDescriptorWithCast(message, prev_desc);
}

std::string
VersionConverter::getJsonStringFromMessage(const Protobuf::Message& message,
                                           envoy::config::core::v3::ApiVersion api_version) {
  DynamicMessagePtr dynamic_message;
  switch (api_version) {
  case envoy::config::core::v3::ApiVersion::AUTO:
  case envoy::config::core::v3::ApiVersion::V2: {
    // TODO(htuch): this works as long as there are no new fields in the v3+
    // DiscoveryRequest. When they are added, we need to do a full v2 conversion
    // and also discard unknown fields. Tracked at
    // https://github.com/envoyproxy/envoy/issues/9619.
    dynamic_message = downgrade(message);
    break;
  }
  case envoy::config::core::v3::ApiVersion::V3: {
    // We need to scrub the hidden fields.
    dynamic_message = std::make_unique<DynamicMessage>();
    dynamic_message->msg_.reset(message.New());
    dynamic_message->msg_->MergeFrom(message);
    VersionUtil::scrubHiddenEnvoyDeprecated(*dynamic_message->msg_);
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  eraseOriginalTypeInformation(*dynamic_message->msg_);
  std::string json;
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  const auto status =
      Protobuf::util::MessageToJsonString(*dynamic_message->msg_, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok(), "");
  return json;
}

void VersionConverter::prepareMessageForGrpcWire(Protobuf::Message& message,
                                                 envoy::config::core::v3::ApiVersion api_version) {
  // TODO(htuch): this works as long as there are no new fields in the v3+
  // DiscoveryRequest. When they are added, we need to do a full v2 conversion
  // and also discard unknown fields. Tracked at
  // https://github.com/envoyproxy/envoy/issues/9619.
  if (api_version == envoy::config::core::v3::ApiVersion::V3) {
    VersionUtil::scrubHiddenEnvoyDeprecated(message);
  }
  eraseOriginalTypeInformation(message);
}

void VersionUtil::scrubHiddenEnvoyDeprecated(Protobuf::Message& message) {
  class HiddenFieldScrubbingProtoVisitor : public ProtobufMessage::ProtoVisitor {
  public:
    const void* onField(Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                        const void*) override {
      const Protobuf::Reflection* reflection = message.GetReflection();
      if (absl::StartsWith(field.name(), DeprecatedFieldShadowPrefix)) {
        reflection->ClearField(&message, &field);
      }
      return nullptr;
    }
  };
  HiddenFieldScrubbingProtoVisitor proto_visitor;
  ProtobufMessage::traverseMutableMessage(proto_visitor, message, nullptr);
}

} // namespace Config
} // namespace Envoy
