#include "extensions/filters/network/thrift_proxy/decoder.h"

#include <unordered_map>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

// MessageBegin -> StructBegin
ProtocolState DecoderStateMachine::messageBegin(Buffer::Instance& buffer) {
  std::string message_name;
  MessageType msg_type;
  int32_t seq_id;
  if (!proto_.readMessageBegin(buffer, message_name, msg_type, seq_id)) {
    return ProtocolState::WaitForData;
  }

  stack_.clear();
  stack_.emplace_back(Frame(ProtocolState::MessageEnd));

  return ProtocolState::StructBegin;
}

// MessageEnd -> Done
ProtocolState DecoderStateMachine::messageEnd(Buffer::Instance& buffer) {
  if (!proto_.readMessageEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  return ProtocolState::Done;
}

// StructBegin -> FieldBegin
ProtocolState DecoderStateMachine::structBegin(Buffer::Instance& buffer) {
  std::string name;
  if (!proto_.readStructBegin(buffer, name)) {
    return ProtocolState::WaitForData;
  }

  return ProtocolState::FieldBegin;
}

// StructEnd -> stack's return state
ProtocolState DecoderStateMachine::structEnd(Buffer::Instance& buffer) {
  if (!proto_.readStructEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  return popReturnState();
}

// FieldBegin -> FieldValue, or
// FieldBegin -> StructEnd (stop field)
ProtocolState DecoderStateMachine::fieldBegin(Buffer::Instance& buffer) {
  std::string name;
  FieldType field_type;
  int16_t field_id;
  if (!proto_.readFieldBegin(buffer, name, field_type, field_id)) {
    return ProtocolState::WaitForData;
  }

  if (field_type == FieldType::Stop) {
    return ProtocolState::StructEnd;
  }

  stack_.emplace_back(Frame(ProtocolState::FieldEnd, field_type));

  return ProtocolState::FieldValue;
}

// FieldValue -> FieldEnd (via stack return state)
ProtocolState DecoderStateMachine::fieldValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());

  Frame& frame = stack_.back();
  return handleValue(buffer, frame.elem_type_, frame.return_state_);
}

// FieldEnd -> FieldBegin
ProtocolState DecoderStateMachine::fieldEnd(Buffer::Instance& buffer) {
  if (!proto_.readFieldEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  popReturnState();

  return ProtocolState::FieldBegin;
}

// ListBegin -> ListValue
ProtocolState DecoderStateMachine::listBegin(Buffer::Instance& buffer) {
  FieldType elem_type;
  uint32_t size;
  if (!proto_.readListBegin(buffer, elem_type, size)) {
    return ProtocolState::WaitForData;
  }

  stack_.emplace_back(Frame(ProtocolState::ListEnd, elem_type, size));

  return ProtocolState::ListValue;
}

// ListValue -> ListValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// ListValue -> ListEnd
ProtocolState DecoderStateMachine::listValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return popReturnState();
  }
  frame.remaining_--;

  return handleValue(buffer, frame.elem_type_, ProtocolState::ListValue);
}

// ListEnd -> stack's return state
ProtocolState DecoderStateMachine::listEnd(Buffer::Instance& buffer) {
  if (!proto_.readListEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  return popReturnState();
}

// MapBegin -> MapKey
ProtocolState DecoderStateMachine::mapBegin(Buffer::Instance& buffer) {
  FieldType key_type, value_type;
  uint32_t size;
  if (!proto_.readMapBegin(buffer, key_type, value_type, size)) {
    return ProtocolState::WaitForData;
  }

  stack_.emplace_back(Frame(ProtocolState::MapEnd, key_type, value_type, size));

  return ProtocolState::MapKey;
}

// MapKey -> MapValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on key type), or
// MapKey -> MapEnd
ProtocolState DecoderStateMachine::mapKey(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return popReturnState();
  }

  return handleValue(buffer, frame.elem_type_, ProtocolState::MapValue);
}

// MapValue -> MapKey, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// MapValue -> MapKey
ProtocolState DecoderStateMachine::mapValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  ASSERT(frame.remaining_ != 0);
  frame.remaining_--;

  return handleValue(buffer, frame.value_type_, ProtocolState::MapKey);
}

// MapEnd -> stack's return state
ProtocolState DecoderStateMachine::mapEnd(Buffer::Instance& buffer) {
  if (!proto_.readMapEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  return popReturnState();
}

// SetBegin -> SetValue
ProtocolState DecoderStateMachine::setBegin(Buffer::Instance& buffer) {
  FieldType elem_type;
  uint32_t size;
  if (!proto_.readSetBegin(buffer, elem_type, size)) {
    return ProtocolState::WaitForData;
  }

  stack_.emplace_back(Frame(ProtocolState::SetEnd, elem_type, size));

  return ProtocolState::SetValue;
}

// SetValue -> SetValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// SetValue -> SetEnd
ProtocolState DecoderStateMachine::setValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return popReturnState();
  }
  frame.remaining_--;

  return handleValue(buffer, frame.elem_type_, ProtocolState::SetValue);
}

// SetEnd -> stack's return state
ProtocolState DecoderStateMachine::setEnd(Buffer::Instance& buffer) {
  if (!proto_.readSetEnd(buffer)) {
    return ProtocolState::WaitForData;
  }

  return popReturnState();
}

ProtocolState DecoderStateMachine::handleValue(Buffer::Instance& buffer, FieldType elem_type,
                                               ProtocolState return_state) {
  switch (elem_type) {
  case FieldType::Bool:
    bool value;
    if (!proto_.readBool(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  case FieldType::Byte: {
    uint8_t value;
    if (!proto_.readByte(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::I16: {
    int16_t value;
    if (!proto_.readInt16(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::I32: {
    int32_t value;
    if (!proto_.readInt32(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::I64: {
    int64_t value;
    if (!proto_.readInt64(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::Double: {
    double value;
    if (!proto_.readDouble(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::String: {
    std::string value;
    if (!proto_.readString(buffer, value)) {
      return ProtocolState::WaitForData;
    }
    break;
  }
  case FieldType::Struct:
    stack_.emplace_back(Frame(return_state));
    return ProtocolState::StructBegin;
  case FieldType::Map:
    stack_.emplace_back(Frame(return_state));
    return ProtocolState::MapBegin;
  case FieldType::List:
    stack_.emplace_back(Frame(return_state));
    return ProtocolState::ListBegin;
  case FieldType::Set:
    stack_.emplace_back(Frame(return_state));
    return ProtocolState::SetBegin;
  default:
    throw EnvoyException(fmt::format("unknown field type {}", static_cast<int8_t>(elem_type)));
  }

  return return_state;
}

ProtocolState DecoderStateMachine::handleState(Buffer::Instance& buffer) {
  switch (state_) {
  case ProtocolState::MessageBegin:
    return messageBegin(buffer);
  case ProtocolState::StructBegin:
    return structBegin(buffer);
  case ProtocolState::StructEnd:
    return structEnd(buffer);
  case ProtocolState::FieldBegin:
    return fieldBegin(buffer);
  case ProtocolState::FieldValue:
    return fieldValue(buffer);
  case ProtocolState::FieldEnd:
    return fieldEnd(buffer);
  case ProtocolState::ListBegin:
    return listBegin(buffer);
  case ProtocolState::ListValue:
    return listValue(buffer);
  case ProtocolState::ListEnd:
    return listEnd(buffer);
  case ProtocolState::MapBegin:
    return mapBegin(buffer);
  case ProtocolState::MapKey:
    return mapKey(buffer);
  case ProtocolState::MapValue:
    return mapValue(buffer);
  case ProtocolState::MapEnd:
    return mapEnd(buffer);
  case ProtocolState::SetBegin:
    return setBegin(buffer);
  case ProtocolState::SetValue:
    return setValue(buffer);
  case ProtocolState::SetEnd:
    return setEnd(buffer);
  case ProtocolState::MessageEnd:
    return messageEnd(buffer);
  default:
    NOT_REACHED;
  }
}

ProtocolState DecoderStateMachine::popReturnState() {
  ASSERT(!stack_.empty());
  ProtocolState return_state = stack_.back().return_state_;
  stack_.pop_back();
  return return_state;
}

ProtocolState DecoderStateMachine::run(Buffer::Instance& buffer) {
  while (state_ != ProtocolState::Done) {
    ProtocolState s = handleState(buffer);
    if (s == ProtocolState::WaitForData) {
      return s;
    }

    state_ = s;
  }

  return state_;
}

Decoder::Decoder(TransportPtr&& transport, ProtocolPtr&& protocol)
    : transport_(std::move(transport)), protocol_(std::move(protocol)), state_machine_{},
      frame_started_(false) {}

void Decoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "thrift: {} bytes available", data.length());

  while (true) {
    if (!frame_started_) {
      // Look for start of next frame.
      if (!transport_->decodeFrameStart(data)) {
        ENVOY_LOG(debug, "thrift: need more data for {} transport start", transport_->name());
        return;
      }
      ENVOY_LOG(debug, "thrift: {} transport started", transport_->name());

      frame_started_ = true;
      state_machine_ = std::make_unique<DecoderStateMachine>(*protocol_);
    }

    ASSERT(state_machine_ != nullptr);

    ENVOY_LOG(debug, "thrift: protocol {}, state {}, {} bytes available", protocol_->name(),
              ProtocolStateNameValues::name(state_machine_->currentState()), data.length());

    ProtocolState rv = state_machine_->run(data);
    if (rv == ProtocolState::WaitForData) {
      ENVOY_LOG(debug, "thrift: wait for data");
      return;
    }

    ASSERT(rv == ProtocolState::Done);

    // Message complete, get decode end of frame.
    if (!transport_->decodeFrameEnd(data)) {
      ENVOY_LOG(debug, "thrift: need more data for {} transport end", transport_->name());
      return;
    }
    ENVOY_LOG(debug, "thrift: {} transport ended", transport_->name());

    // Reset for next frame.
    state_machine_ = nullptr;
    frame_started_ = false;
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
