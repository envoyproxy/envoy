#include "source/common/protobuf/visitor.h"

#include <vector>

#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor_helper.h"

#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

absl::Status traverseMessageWorker(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                                   std::vector<const Protobuf::Message*>& parents,
                                   bool was_any_or_top_level, bool recurse_into_any) {
  visitor.onMessage(message, parents, was_any_or_top_level);

  // If told to recurse into Any messages, do that here and skip the rest of the function.
  if (recurse_into_any) {
    std::unique_ptr<Protobuf::Message> inner_message;
    absl::string_view target_type_url;

    if (message.GetTypeName() == "google.protobuf.Any") {
      auto* any_message = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(&message);
      inner_message = Helper::typeUrlToMessage(any_message->type_url());
      target_type_url = any_message->type_url();
      // inner_message must be valid as parsing would have already failed to load if there was an
      // invalid type_url.
      RETURN_IF_NOT_OK(MessageUtil::unpackTo(*any_message, *inner_message));
    } else if (message.GetTypeName() == "xds.type.v3.TypedStruct") {
      auto output_or_error = Helper::convertTypedStruct<xds::type::v3::TypedStruct>(message);
      RETURN_IF_NOT_OK_REF(output_or_error.status());
      std::tie(inner_message, target_type_url) = std::move(output_or_error.value());
    } else if (message.GetTypeName() == "udpa.type.v1.TypedStruct") {
      auto output_or_error = Helper::convertTypedStruct<udpa::type::v1::TypedStruct>(message);
      RETURN_IF_NOT_OK_REF(output_or_error.status());
      std::tie(inner_message, target_type_url) = std::move(output_or_error.value());
    }

    if (inner_message != nullptr) {
      // Push the Any message as a wrapper.
      Helper::ScopedMessageParents scoped_parents(parents, message);
      return traverseMessageWorker(visitor, *inner_message, parents, true, recurse_into_any);
    } else if (!target_type_url.empty()) {
      return absl::InvalidArgumentError(
          fmt::format("Invalid type_url '{}' during traversal", target_type_url));
    }
  }
  Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
  const Protobuf::Descriptor* descriptor = reflectable_message->GetDescriptor();
  const Protobuf::Reflection* reflection = reflectable_message->GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    visitor.onField(message, *field);

    // If this is a message, recurse in to the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      Helper::ScopedMessageParents scoped_parents(parents, message);

      if (field->is_repeated()) {
        const int size = reflection->FieldSize(*reflectable_message, field);
        for (int j = 0; j < size; ++j) {
          RETURN_IF_NOT_OK(traverseMessageWorker(
              visitor, reflection->GetRepeatedMessage(*reflectable_message, field, j), parents,
              false, recurse_into_any));
        }
      } else if (reflection->HasField(*reflectable_message, field)) {
        RETURN_IF_NOT_OK(traverseMessageWorker(visitor,
                                               reflection->GetMessage(*reflectable_message, field),
                                               parents, false, recurse_into_any));
      }
    }
  }
  return absl::OkStatus();
}

} // namespace

absl::Status traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                             bool recurse_into_any) {
  std::vector<const Protobuf::Message*> parents;
  return traverseMessageWorker(visitor, message, parents, true, recurse_into_any);
}

} // namespace ProtobufMessage
} // namespace Envoy
