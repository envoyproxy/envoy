#include "extensions/filters/network/thrift_proxy/decoder.h"

#include <unordered_map>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

// MessageBegin -> StructBegin
DecoderStateMachine::DecoderStatus DecoderStateMachine::messageBegin(Buffer::Instance& buffer) {
  if (!proto_.readMessageBegin(buffer, *metadata_)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  stack_.clear();
  stack_.emplace_back(Frame(ProtocolState::MessageEnd));

  return DecoderStatus(ProtocolState::StructBegin, handler_.messageBegin(metadata_));
}

// MessageEnd -> Done
DecoderStateMachine::DecoderStatus DecoderStateMachine::messageEnd(Buffer::Instance& buffer) {
  if (!proto_.readMessageEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  return DecoderStatus(ProtocolState::Done, handler_.messageEnd());
}

// StructBegin -> FieldBegin
DecoderStateMachine::DecoderStatus DecoderStateMachine::structBegin(Buffer::Instance& buffer) {
  std::string name;
  if (!proto_.readStructBegin(buffer, name)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  return DecoderStatus(ProtocolState::FieldBegin, handler_.structBegin(absl::string_view(name)));
}

// StructEnd -> stack's return state
DecoderStateMachine::DecoderStatus DecoderStateMachine::structEnd(Buffer::Instance& buffer) {
  if (!proto_.readStructEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  ProtocolState next_state = popReturnState();
  return DecoderStatus(next_state, handler_.structEnd());
}

// FieldBegin -> FieldValue, or
// FieldBegin -> StructEnd (stop field)
DecoderStateMachine::DecoderStatus DecoderStateMachine::fieldBegin(Buffer::Instance& buffer) {
  std::string name;
  FieldType field_type;
  int16_t field_id;
  if (!proto_.readFieldBegin(buffer, name, field_type, field_id)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  if (field_type == FieldType::Stop) {
    return DecoderStatus(ProtocolState::StructEnd, FilterStatus::Continue);
  }

  stack_.emplace_back(Frame(ProtocolState::FieldEnd, field_type));

  return DecoderStatus(ProtocolState::FieldValue,
                       handler_.fieldBegin(absl::string_view(name), field_type, field_id));
}

// FieldValue -> FieldEnd (via stack return state)
DecoderStateMachine::DecoderStatus DecoderStateMachine::fieldValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());

  Frame& frame = stack_.back();
  return handleValue(buffer, frame.elem_type_, frame.return_state_);
}

// FieldEnd -> FieldBegin
DecoderStateMachine::DecoderStatus DecoderStateMachine::fieldEnd(Buffer::Instance& buffer) {
  if (!proto_.readFieldEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  popReturnState();

  return DecoderStatus(ProtocolState::FieldBegin, handler_.fieldEnd());
}

// ListBegin -> ListValue
DecoderStateMachine::DecoderStatus DecoderStateMachine::listBegin(Buffer::Instance& buffer) {
  FieldType elem_type;
  uint32_t size;
  if (!proto_.readListBegin(buffer, elem_type, size)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  stack_.emplace_back(Frame(ProtocolState::ListEnd, elem_type, size));

  return DecoderStatus(ProtocolState::ListValue, handler_.listBegin(elem_type, size));
}

// ListValue -> ListValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// ListValue -> ListEnd
DecoderStateMachine::DecoderStatus DecoderStateMachine::listValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return DecoderStatus(popReturnState(), FilterStatus::Continue);
  }
  frame.remaining_--;

  return handleValue(buffer, frame.elem_type_, ProtocolState::ListValue);
}

// ListEnd -> stack's return state
DecoderStateMachine::DecoderStatus DecoderStateMachine::listEnd(Buffer::Instance& buffer) {
  if (!proto_.readListEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  ProtocolState next_state = popReturnState();
  return DecoderStatus(next_state, handler_.listEnd());
}

// MapBegin -> MapKey
DecoderStateMachine::DecoderStatus DecoderStateMachine::mapBegin(Buffer::Instance& buffer) {
  FieldType key_type, value_type;
  uint32_t size;
  if (!proto_.readMapBegin(buffer, key_type, value_type, size)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  stack_.emplace_back(Frame(ProtocolState::MapEnd, key_type, value_type, size));

  return DecoderStatus(ProtocolState::MapKey, handler_.mapBegin(key_type, value_type, size));
}

// MapKey -> MapValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on key type), or
// MapKey -> MapEnd
DecoderStateMachine::DecoderStatus DecoderStateMachine::mapKey(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return DecoderStatus(popReturnState(), FilterStatus::Continue);
  }

  return handleValue(buffer, frame.elem_type_, ProtocolState::MapValue);
}

// MapValue -> MapKey, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// MapValue -> MapKey
DecoderStateMachine::DecoderStatus DecoderStateMachine::mapValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  ASSERT(frame.remaining_ != 0);
  frame.remaining_--;

  return handleValue(buffer, frame.value_type_, ProtocolState::MapKey);
}

// MapEnd -> stack's return state
DecoderStateMachine::DecoderStatus DecoderStateMachine::mapEnd(Buffer::Instance& buffer) {
  if (!proto_.readMapEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  ProtocolState next_state = popReturnState();
  return DecoderStatus(next_state, handler_.mapEnd());
}

// SetBegin -> SetValue
DecoderStateMachine::DecoderStatus DecoderStateMachine::setBegin(Buffer::Instance& buffer) {
  FieldType elem_type;
  uint32_t size;
  if (!proto_.readSetBegin(buffer, elem_type, size)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  stack_.emplace_back(Frame(ProtocolState::SetEnd, elem_type, size));

  return DecoderStatus(ProtocolState::SetValue, handler_.setBegin(elem_type, size));
}

// SetValue -> SetValue, ListBegin, MapBegin, SetBegin, StructBegin (depending on value type), or
// SetValue -> SetEnd
DecoderStateMachine::DecoderStatus DecoderStateMachine::setValue(Buffer::Instance& buffer) {
  ASSERT(!stack_.empty());
  Frame& frame = stack_.back();
  if (frame.remaining_ == 0) {
    return DecoderStatus(popReturnState(), FilterStatus::Continue);
  }
  frame.remaining_--;

  return handleValue(buffer, frame.elem_type_, ProtocolState::SetValue);
}

// SetEnd -> stack's return state
DecoderStateMachine::DecoderStatus DecoderStateMachine::setEnd(Buffer::Instance& buffer) {
  if (!proto_.readSetEnd(buffer)) {
    return DecoderStatus(ProtocolState::WaitForData);
  }

  ProtocolState next_state = popReturnState();
  return DecoderStatus(next_state, handler_.setEnd());
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleValue(Buffer::Instance& buffer,
                                                                    FieldType elem_type,
                                                                    ProtocolState return_state) {
  switch (elem_type) {
  case FieldType::Bool: {
    bool value{};
    if (proto_.readBool(buffer, value)) {
      return DecoderStatus(return_state, handler_.boolValue(value));
    }
    break;
  }
  case FieldType::Byte: {
    uint8_t value{};
    if (proto_.readByte(buffer, value)) {
      return DecoderStatus(return_state, handler_.byteValue(value));
    }
    break;
  }
  case FieldType::I16: {
    int16_t value{};
    if (proto_.readInt16(buffer, value)) {
      return DecoderStatus(return_state, handler_.int16Value(value));
    }
    break;
  }
  case FieldType::I32: {
    int32_t value{};
    if (proto_.readInt32(buffer, value)) {
      return DecoderStatus(return_state, handler_.int32Value(value));
    }
    break;
  }
  case FieldType::I64: {
    int64_t value{};
    if (proto_.readInt64(buffer, value)) {
      return DecoderStatus(return_state, handler_.int64Value(value));
    }
    break;
  }
  case FieldType::Double: {
    double value{};
    if (proto_.readDouble(buffer, value)) {
      return DecoderStatus(return_state, handler_.doubleValue(value));
    }
    break;
  }
  case FieldType::String: {
    std::string value;
    if (proto_.readString(buffer, value)) {
      return DecoderStatus(return_state, handler_.stringValue(value));
    }
    break;
  }
  case FieldType::Struct:
    stack_.emplace_back(Frame(return_state));
    return DecoderStatus(ProtocolState::StructBegin, FilterStatus::Continue);
  case FieldType::Map:
    stack_.emplace_back(Frame(return_state));
    return DecoderStatus(ProtocolState::MapBegin, FilterStatus::Continue);
  case FieldType::List:
    stack_.emplace_back(Frame(return_state));
    return DecoderStatus(ProtocolState::ListBegin, FilterStatus::Continue);
  case FieldType::Set:
    stack_.emplace_back(Frame(return_state));
    return DecoderStatus(ProtocolState::SetBegin, FilterStatus::Continue);
  default:
    throw EnvoyException(fmt::format("unknown field type {}", static_cast<int8_t>(elem_type)));
  }

  return DecoderStatus(ProtocolState::WaitForData);
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleState(Buffer::Instance& buffer) {
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
    NOT_REACHED_GCOVR_EXCL_LINE;
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
    ENVOY_LOG(trace, "thrift: state {}, {} bytes available", ProtocolStateNameValues::name(state_),
              buffer.length());

    DecoderStatus s = handleState(buffer);
    if (s.next_state_ == ProtocolState::WaitForData) {
      return ProtocolState::WaitForData;
    }

    state_ = s.next_state_;

    ASSERT(s.filter_status_.has_value());
    if (s.filter_status_.value() == FilterStatus::StopIteration) {
      return ProtocolState::StopIteration;
    }
  }

  return state_;
}

Decoder::Decoder(Transport& transport, Protocol& protocol, DecoderCallbacks& callbacks)
    : transport_(transport), protocol_(protocol), callbacks_(callbacks) {}

void Decoder::complete() {
  request_.reset();
  state_machine_ = nullptr;
  frame_started_ = false;
  frame_ended_ = false;
}

FilterStatus Decoder::onData(Buffer::Instance& data, bool& buffer_underflow) {
  ENVOY_LOG(debug, "thrift: {} bytes available", data.length());
  buffer_underflow = false;

  if (frame_ended_) {
    // Continuation after filter stopped iteration on transportComplete callback.
    complete();
    buffer_underflow = (data.length() == 0);
    return FilterStatus::Continue;
  }

  if (!frame_started_) {
    // Look for start of next frame.
    if (!metadata_) {
      metadata_ = std::make_shared<MessageMetadata>();
    }

    if (!transport_.decodeFrameStart(data, *metadata_)) {
      ENVOY_LOG(debug, "thrift: need more data for {} transport start", transport_.name());
      buffer_underflow = true;
      return FilterStatus::Continue;
    }
    ENVOY_LOG(debug, "thrift: {} transport started", transport_.name());

    if (metadata_->hasProtocol()) {
      if (protocol_.type() == ProtocolType::Auto) {
        protocol_.setType(metadata_->protocol());
        ENVOY_LOG(debug, "thrift: {} transport forced {} protocol", transport_.name(),
                  protocol_.name());
      } else if (metadata_->protocol() != protocol_.type()) {
        throw EnvoyException(fmt::format("transport reports protocol {}, but configured for {}",
                                         ProtocolNames::get().fromType(metadata_->protocol()),
                                         ProtocolNames::get().fromType(protocol_.type())));
      }
    }
    if (metadata_->hasAppException()) {
      AppExceptionType ex_type = metadata_->appExceptionType();
      std::string ex_msg = metadata_->appExceptionMessage();
      // Force new metadata if we get called again.
      metadata_.reset();
      throw AppException(ex_type, ex_msg);
    }

    request_ = std::make_unique<ActiveRequest>(callbacks_.newDecoderEventHandler());
    frame_started_ = true;
    state_machine_ =
        std::make_unique<DecoderStateMachine>(protocol_, metadata_, request_->handler_);

    if (request_->handler_.transportBegin(metadata_) == FilterStatus::StopIteration) {
      return FilterStatus::StopIteration;
    }
  }

  ASSERT(state_machine_ != nullptr);

  ENVOY_LOG(debug, "thrift: protocol {}, state {}, {} bytes available", protocol_.name(),
            ProtocolStateNameValues::name(state_machine_->currentState()), data.length());

  ProtocolState rv = state_machine_->run(data);
  if (rv == ProtocolState::WaitForData) {
    ENVOY_LOG(debug, "thrift: wait for data");
    buffer_underflow = true;
    return FilterStatus::Continue;
  } else if (rv == ProtocolState::StopIteration) {
    ENVOY_LOG(debug, "thrift: wait for continuation");
    return FilterStatus::StopIteration;
  }

  ASSERT(rv == ProtocolState::Done);

  // Message complete, decode end of frame.
  if (!transport_.decodeFrameEnd(data)) {
    ENVOY_LOG(debug, "thrift: need more data for {} transport end", transport_.name());
    buffer_underflow = true;
    return FilterStatus::Continue;
  }

  frame_ended_ = true;
  metadata_.reset();

  ENVOY_LOG(debug, "thrift: {} transport ended", transport_.name());
  if (request_->handler_.transportEnd() == FilterStatus::StopIteration) {
    return FilterStatus::StopIteration;
  }

  // Reset for next frame.
  complete();
  buffer_underflow = (data.length() == 0);
  return FilterStatus::Continue;
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
