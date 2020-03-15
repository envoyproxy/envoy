#include "common/protobuf/visitor.h"

namespace Envoy {
namespace ProtobufMessage {

void traverseMutableMessage(ProtoVisitor& visitor, Protobuf::Message& message, const void* ctxt) {
  visitor.onMessage(message, ctxt);
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    const void* field_ctxt = visitor.onField(message, *field, ctxt);
    // If this is a message, recurse to visit fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMutableMessage(visitor, *reflection->MutableRepeatedMessage(&message, field, j),
                                 field_ctxt);
        }
      } else if (reflection->HasField(message, field)) {
        traverseMutableMessage(visitor, *reflection->MutableMessage(&message, field), field_ctxt);
      }
    }
  }
}
void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     const void* ctxt) {
  visitor.onMessage(message, ctxt);
  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const Protobuf::FieldDescriptor* field = descriptor->field(i);
    const void* field_ctxt = visitor.onField(message, *field, ctxt);
    // If this is a message, recurse to scrub deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          traverseMessage(visitor, reflection->GetRepeatedMessage(message, field, j), field_ctxt);
        }
      } else if (reflection->HasField(message, field)) {
        traverseMessage(visitor, reflection->GetMessage(message, field), field_ctxt);
      }
    }
  }
}

} // namespace ProtobufMessage
} // namespace Envoy
