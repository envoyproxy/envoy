#pragma once

#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "absl/types/span.h"

namespace Envoy {
namespace ProtobufMessage {

class ConstProtoVisitor {
public:
  virtual ~ConstProtoVisitor() = default;

  // Invoked when a field is visited, with the message, field descriptor, context, and
  // visited parents.
  virtual void onField(const Protobuf::Message&, const Protobuf::FieldDescriptor&,
                       absl::Span<const Protobuf::Message* const>) {}

  // Invoked when a message is visited, with the message and visited parents.
  virtual void onMessage(const Protobuf::Message&, absl::Span<const Protobuf::Message* const>) {}
};

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     std::vector<const Protobuf::Message*>& parents);

inline void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message) {
  std::vector<const Protobuf::Message*> parents;
  traverseMessage(visitor, message, parents);
}

} // namespace ProtobufMessage
} // namespace Envoy
