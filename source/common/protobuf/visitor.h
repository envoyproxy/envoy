#pragma once

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace ProtobufMessage {

class ConstProtoVisitor {
public:
  virtual ~ConstProtoVisitor() = default;

  // Invoked when a field is visited, with the message, and field descriptor.
  virtual void onField(const Protobuf::Message&, const Protobuf::FieldDescriptor&) {}

  // Invoked when a message is visited, with the message.
  virtual void onMessage(const Protobuf::Message&) {}
};

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     bool recurse_into_any);

} // namespace ProtobufMessage
} // namespace Envoy
