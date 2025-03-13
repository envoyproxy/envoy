#include "test/fuzz/mutable_visitor.h"

#include <vector>

#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor_helper.h"

#include "absl/cleanup/cleanup.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

void traverseMessageWorkerExt(ProtoVisitor& visitor, Protobuf::Message& message,
                              std::vector<const Protobuf::Message*>& parents,
                              bool was_any_or_top_level, bool recurse_into_any,
                              absl::string_view field_name) {
  visitor.onEnterMessage(message, parents, was_any_or_top_level, field_name);
  absl::Cleanup message_leaver = [&visitor, &parents, &message, was_any_or_top_level, field_name] {
    visitor.onLeaveMessage(message, parents, was_any_or_top_level, field_name);
  };

  // If told to recurse into Any messages, do that here and skip the rest of the function.
  if (recurse_into_any) {
    std::unique_ptr<Protobuf::Message> inner_message;
    absl::string_view target_type_url;

    if (message.GetDescriptor()->full_name() == "google.protobuf.Any") {
      auto* any_message = Protobuf::DynamicCastMessage<ProtobufWkt::Any>(&message);
      inner_message = Helper::typeUrlToMessage(any_message->type_url());
      target_type_url = any_message->type_url();
      if (inner_message) {
        THROW_IF_NOT_OK(MessageUtil::unpackTo(*any_message, *inner_message));
      }
    } else if (message.GetDescriptor()->full_name() == "xds.type.v3.TypedStruct") {
      std::tie(inner_message, target_type_url) =
          Helper::convertTypedStruct<xds::type::v3::TypedStruct>(message).value();
    } else if (message.GetDescriptor()->full_name() == "udpa.type.v1.TypedStruct") {
      std::tie(inner_message, target_type_url) =
          (Helper::convertTypedStruct<udpa::type::v1::TypedStruct>(message)).value();
    }

    if (inner_message != nullptr) {
      // Push the Any message as a wrapper.
      Helper::ScopedMessageParents scoped_parents(parents, message);
      traverseMessageWorkerExt(visitor, *inner_message, parents, true, recurse_into_any,
                               absl::string_view());
      return;
    } else if (!target_type_url.empty()) {
      throw EnvoyException(fmt::format("Invalid type_url '{}' during traversal", target_type_url));
    }
  }

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    visitor.onField(message, *field, parents);

    // If this is a message, recurse in to the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      Helper::ScopedMessageParents scoped_parents(parents, message);

      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMessageWorkerExt(visitor, *reflection->MutableRepeatedMessage(&message, field, j),
                                   parents, false, recurse_into_any, field->name());
        }
      } else if (reflection->HasField(message, field)) {
        traverseMessageWorkerExt(visitor, *reflection->MutableMessage(&message, field), parents,
                                 false, recurse_into_any, field->name());
      }
    }
  }
}

} // namespace

void traverseMessage(ProtoVisitor& visitor, Protobuf::Message& message, bool recurse_into_any) {
  std::vector<const Protobuf::Message*> parents;
  traverseMessageWorkerExt(visitor, message, parents, true, recurse_into_any, "envoy");
}

} // namespace ProtobufMessage
} // namespace Envoy
