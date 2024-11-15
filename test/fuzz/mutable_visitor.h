#pragma once

#include "envoy/common/pure.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/span.h"

namespace Envoy {
namespace ProtobufMessage {

class ProtoVisitor {
public:
  virtual ~ProtoVisitor() = default;

  // Invoked when a field is visited, with the message, and field descriptor.
  virtual void onField(Protobuf::Message&, const Protobuf::FieldDescriptor&,
                       const absl::Span<const Protobuf::Message* const>) PURE;

  // Invoked when a message is visited, with the message and visited parents.
  // @param was_any_or_top_level supplies whether the message was either the top level message or an
  //                             Any before being unpacked for further recursion. The latter can
  //                             only be achieved by using recurse_into_any.
  virtual void onEnterMessage(Protobuf::Message&, absl::Span<const Protobuf::Message* const>,
                              bool was_any_or_top_level, absl::string_view field_name) PURE;
  virtual void onLeaveMessage(Protobuf::Message&, absl::Span<const Protobuf::Message* const>,
                              bool was_any_or_top_level, absl::string_view field_name) PURE;
};

void traverseMessage(ProtoVisitor& visitor, Protobuf::Message& message, bool recurse_into_any);

} // namespace ProtobufMessage
} // namespace Envoy
