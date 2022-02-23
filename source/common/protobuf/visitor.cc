#include "source/common/protobuf/visitor.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace ProtobufMessage {

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     bool recurse_into_any) {
  visitor.onMessage(message);

  // If told to recurse into Any messages, do that here and skip the rest of the function.
  if (recurse_into_any && message.GetDescriptor()->full_name() == "google.protobuf.Any") {
    auto* any_message = Protobuf::DynamicCastToGenerated<ProtobufWkt::Any>(&message);
    const absl::string_view inner_type_name =
        TypeUtil::typeUrlToDescriptorFullName(any_message->type_url());
    const Protobuf::Descriptor* inner_descriptor =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            static_cast<std::string>(inner_type_name));
    auto* inner_message_prototype =
        Protobuf::MessageFactory::generated_factory()->GetPrototype(inner_descriptor);
    auto* inner_message = inner_message_prototype->New();
    MessageUtil::unpackTo(*any_message, *inner_message);
    traverseMessage(visitor, *inner_message, recurse_into_any);
    return;
  }

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    visitor.onField(message, *field);
    // If this is a message, recurse to scrub deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMessage(visitor, reflection->GetRepeatedMessage(message, field, j),
                          recurse_into_any);
        }
      } else if (reflection->HasField(message, field)) {
        traverseMessage(visitor, reflection->GetMessage(message, field), recurse_into_any);
      }
    }
  }
}

} // namespace ProtobufMessage
} // namespace Envoy
