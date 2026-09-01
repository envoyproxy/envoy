#pragma once

#include <optional>
#include <stack>

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
  struct Options {
    // Whether to emit a leading @type naming the message.
    bool emit_type_url = false;

    // Whether the keys are the proto field names or the lowerCamelCase ProtoJSON defaults to.
    // https://protobuf.dev/programming-guides/json/#field-names
    bool preserve_proto_field_names = false;

    // Whether the fields the API marks sensitive are emitted, or replaced the way
    // MessageUtil::redact replaces them. Redacted, a sensitive field emits:
    //
    //   string token                        "token": "[redacted]"
    //   bytes key                           "key": "<base64 of [redacted]>"
    //   Credentials creds                   "creds": {...}, redacted field by field
    //   uint32 port                         both key and value dropped
    //   int64 id                            both key and value dropped
    //   map<string, uint32> ports           "ports": {"http": 0}
    //   google.protobuf.Int64Value ttl      "ttl": "0"
    //   google.protobuf.StringValue name    "name": "[redacted]"
    bool redact_sensitive_fields = false;
  };

  // Emits `message` as an object opened in `level`, which must be expecting a value.
  MessageStreamer(const Protobuf::Message& message, BufferStreamer::Level& level, Options options);
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
    // The next field of `fields_` to start.
    uint32_t next_field_{0};
    // The next element of the open repeated field or map to emit.
    int next_element_{0};
    // The object the frame emits its fields into.
    BufferStreamer::MapPtr map_;
    // An array or a map, holding the elements of the repeated field being emitted.
    BufferStreamer::LevelPtr elements_;
    // The field `elements_` holds, non-null exactly while `elements_` is open.
    const Protobuf::FieldDescriptor* elements_field_{nullptr};
    // Non-null when the frame owns `message_`.
    ProtobufTypes::MessagePtr owned_;
    // Set when the field this frame was reached through is sensitive, so all of it is.
    bool ancestor_is_sensitive_{false};
    // Whether the field last started is sensitive, held so its elements can read it back.
    bool field_is_sensitive_{false};
  };

  // Emits the next element of the open repeated field or map, closing it once elements run out.
  // A message emitted piece wise, such as an ordinary nested message or an unpacked Any, pushes a
  // frame onto `stack_`.
  void emitNextElement(Frame& frame);

  // Starts the next field and emits a scalar or opens `elements_` for `emitNextElement` to fill.
  // A message emitted piece wise, such as an ordinary nested message or an unpacked Any, pushes a
  // frame onto `stack_`.
  void emitNextField(Frame& frame);

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

  // Emits the redacted form of `field`'s value, a replacement for text and the type's default for
  // anything else. Messages are handled by walking them, not redacting as whole. Leaf values that
  // are redacted are handled directly here. Messages with hierarchy (repeated fields and maps) are
  // recursed fully, and the redactions occur only at leaves.
  void emitRedactedValue(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                         BufferStreamer::Level& level);

  // Emits one value of `field`, or pushes a frame for it. `index` is which element of a repeated
  // field to emit, or nullopt for a field that is not repeated.
  void emitValue(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                 std::optional<int> index, BufferStreamer::Level& level, bool is_sensitive);

  // Emits entry `index` of the map `field` as a key and a value in `entries`.
  void emitMapEntry(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                    int index, BufferStreamer::Map& entries, bool is_sensitive);

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

  const Options options_;
  std::stack<Frame> stack_;
  std::string scratch_;
};

} // namespace Json
} // namespace Envoy
