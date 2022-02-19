#include "source/common/protobuf/visitor.h"

namespace Envoy {
namespace ProtobufMessage {

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     std::vector<const Protobuf::Message*>& parents) {
  visitor.onMessage(message, parents);

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    visitor.onField(message, *field, parents);

    // If this is a message, recurse to scrub deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      parents.push_back(&message);

      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMessage(visitor, reflection->GetRepeatedMessage(message, field, j), parents);
        }
      } else if (reflection->HasField(message, field)) {
        traverseMessage(visitor, reflection->GetMessage(message, field), parents);
      }

      parents.pop_back();
    }
  }
}

} // namespace ProtobufMessage
} // namespace Envoy
