#pragma once

#include <deque>

#include "source/common/json/json_streamer.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Json {

/**
 * Emits a protobuf message in ProtoJSON (see https://protobuf.dev/programming-guides/json/) one
 * piece at a time, walking it with reflection and an explicit stack.
 */
class MessageStreamer {
public:
  // Whether to emit a leading @type naming the message.
  enum class TypeUrl { Omit, Emit };

  // Whether the keys are the proto field names or the lowerCamelCase ProtoJSON defaults to.
  // https://protobuf.dev/programming-guides/json/#field-names
  enum class FieldNames { Proto, LowerCamelCase };

  // Whether the fields the API marks sensitive are emitted, or replaced the way
  // MessageUtil::redact replaces them.
  enum class Sensitive { Emit, Redact };

  // Emits `message` as an object opened in `level`, which must be expecting a value.
  MessageStreamer(const Protobuf::Message& message, BufferStreamer::Level& level, TypeUrl type_url,
                  FieldNames field_names, Sensitive sensitive);
  ~MessageStreamer();

  /**
   * Emits the next piece of the message: one scalar field, one element of a repeated field, or the
   * opening or closing of a nested object or array.
   * @return false once the whole message has been emitted.
   */
  bool next();

private:
  struct Frame {
    Frame(const Protobuf::Message& message, BufferStreamer::MapPtr map)
        : message_(message), map_(std::move(map)) {
      message_.GetReflection()->ListFields(message, &fields_);
    }

    const Protobuf::Message& message_;
    // The set fields of `message_`, in field number order.
    std::vector<const Protobuf::FieldDescriptor*> fields_;
    uint32_t next_field_{0};
    // Which element of the current repeated field or map comes next.
    int next_element_{0};
    // The object the frame emits its fields into.
    BufferStreamer::MapPtr map_;
    // An array or a map, holding the elements of the repeated field being emitted.
    BufferStreamer::LevelPtr elements_;
    // Non-null when the frame owns `message_`.
    ProtobufTypes::MessagePtr owned_;
    // Set when the field this frame was reached through is sensitive, so all of it is.
    bool ancestor_is_sensitive_{false};
    // Whether fields_[next_field_] is sensitive, held so its elements can read it back.
    bool field_is_sensitive_{false};
  };

  void nextElement(Frame& frame);

  // Starts the next field, opening a level for it when it is repeated or a map.
  void startField(Frame& frame);

  // Emits `message` in `level` under `@type` if `type_url` is not empty.
  // A message with a special representation goes under `value`, anything else creates a new frame,
  // which takes `owned` over when the streamer is the one holding the message.
  // https://protobuf.dev/programming-guides/json/#any
  void emitNamedMessage(const Protobuf::Message& message, BufferStreamer::Level& level,
                        absl::string_view type_url, bool is_sensitive,
                        ProtobufTypes::MessagePtr owned = nullptr);

  // Pushes a frame emitting `message` as an object opened in `level`.
  Frame& pushFrame(const Protobuf::Message& message, BufferStreamer::Level& level,
                   bool ancestor_is_sensitive);

  Frame& pushOwnedFrame(ProtobufTypes::MessagePtr message, BufferStreamer::Level& level,
                        bool ancestor_is_sensitive);

  // Emits one value of `field`, or pushes a frame for it. `index` is which element of a repeated
  // field to emit, or -1 for a field that is not repeated.
  void emitValue(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                 int index, BufferStreamer::Level& level, bool is_sensitive);

  // Emits the value of a message-typed field, which is either a special representation or a new
  // frame.
  void emitMessage(const Protobuf::Message& message, BufferStreamer::Level& level,
                   bool is_sensitive);

  // Emits an Any as its type url and the message it packs, or the type url alone when the payload
  // cannot be unpacked.
  void emitAny(const Protobuf::Message& message, BufferStreamer::Level& level, bool is_sensitive);

  // Emits the special representation of a well known type through protobuf's printer.
  void emitSpecialRepresentation(const Protobuf::Message& message, BufferStreamer::Level& level,
                                 bool is_sensitive);

  // Whether the keys are json_name(), for the whole walk.
  const bool json_names_;
  // Whether sensitive fields are redacted, for the whole walk.
  const bool redact_;
  std::deque<Frame> stack_;
  std::string scratch_;
};

} // namespace Json
} // namespace Envoy
