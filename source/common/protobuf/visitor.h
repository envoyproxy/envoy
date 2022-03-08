#pragma once

#include <vector>

#include "envoy/common/pure.h"

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
                       absl::Span<const Protobuf::Message* const>) PURE;

  // Invoked when a message is visited, with the message and visited parents.
  // @param was_any_or_top_level supplies whether the message was either the top level message or an
  //                             Any before being unpacked for further recursion. The latter can
  //                             only be achieved by using recurse_into_any.
  virtual void onMessage(const Protobuf::Message&, absl::Span<const Protobuf::Message* const>,
                         bool was_any_or_top_level) PURE;
};

void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                     std::vector<const Protobuf::Message*>& parents, bool recurse_into_any);

inline void traverseMessage(ConstProtoVisitor& visitor, const Protobuf::Message& message,
                            bool recurse_into_any) {
  std::vector<const Protobuf::Message*> parents;
  traverseMessage(visitor, message, parents, recurse_into_any);
}

} // namespace ProtobufMessage
} // namespace Envoy
