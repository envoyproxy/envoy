#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(MessageBegin)                                                                           \
  FUNCTION(MessageEnd)                                                                             \
  FUNCTION(StructBegin)                                                                            \
  FUNCTION(StructEnd)                                                                              \
  FUNCTION(FieldBegin)                                                                             \
  FUNCTION(FieldValue)                                                                             \
  FUNCTION(FieldEnd)                                                                               \
  FUNCTION(MapBegin)                                                                               \
  FUNCTION(MapKey)                                                                                 \
  FUNCTION(MapValue)                                                                               \
  FUNCTION(MapEnd)                                                                                 \
  FUNCTION(ListBegin)                                                                              \
  FUNCTION(ListValue)                                                                              \
  FUNCTION(ListEnd)                                                                                \
  FUNCTION(SetBegin)                                                                               \
  FUNCTION(SetValue)                                                                               \
  FUNCTION(SetEnd)                                                                                 \
  FUNCTION(Done)

/**
 * ProtocolState represents a set of states used in a state machine to decode Thrift requests
 * and responses.
 */
enum class ProtocolState { ALL_PROTOCOL_STATES(GENERATE_ENUM) };

class ProtocolStateNameValues {
public:
  static const std::string& name(ProtocolState state) {
    size_t i = static_cast<size_t>(state);
    ASSERT(i < names().size());
    return names()[i];
  }

private:
  static const std::vector<std::string>& names() {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {ALL_PROTOCOL_STATES(GENERATE_STRING)});
  }
};

/**
 * DecoderStateMachine is the Thrift message state machine as described in
 * source/extensions/filters/network/thrift_proxy/docs.
 */
class DecoderStateMachine {
public:
  DecoderStateMachine(Protocol& proto, MessageMetadataSharedPtr& metadata,
                      DecoderEventHandler& handler)
      : proto_(proto), metadata_(metadata), handler_(handler), state_(ProtocolState::MessageBegin) {
  }

  /**
   * Consumes as much data from the configured Buffer as possible and executes the decoding state
   * machine. Returns ProtocolState::WaitForData if more data is required to complete processing of
   * a message. Returns ProtocolState::Done when the end of a message is successfully processed.
   * Once the Done state is reached, further invocations of run return immediately with Done.
   *
   * @param buffer a buffer containing the remaining data to be processed
   * @return ProtocolState returns with ProtocolState::WaitForData or ProtocolState::Done
   * @throw Envoy Exception if thrown by the underlying Protocol
   */
  ProtocolState run(Buffer::Instance& buffer);

  /**
   * @return the current ProtocolState
   */
  ProtocolState currentState() const { return state_; }

  /**
   * Set the current state. Used for testing only.
   */
  void setCurrentState(ProtocolState state) { state_ = state; }

private:
  /**
   * Frame encodes information about the return state for nested elements, container element types,
   * and the number of remaining container elements.
   */
  struct Frame {
    Frame(ProtocolState state) : return_state_(state), elem_type_{}, value_type_{}, remaining_(0) {}
    Frame(ProtocolState state, FieldType elem_type)
        : return_state_(state), elem_type_(elem_type), value_type_{}, remaining_{} {}
    Frame(ProtocolState state, FieldType elem_type, uint32_t remaining)
        : return_state_(state), elem_type_(elem_type), value_type_{}, remaining_(remaining) {}
    Frame(ProtocolState state, FieldType key_type, FieldType value_type, uint32_t remaining)
        : return_state_(state), elem_type_(key_type), value_type_(value_type),
          remaining_(remaining) {}

    // Structs, lists, maps, and sets may be recursively nested in any combination. This field
    // indicates which state to return to at the completion of each of those types.
    const ProtocolState return_state_;

    // Indicates the element type for lists and sets or the key type for a map.
    const FieldType elem_type_;

    // Indicates the value type for a map.
    const FieldType value_type_;

    // Indicates the number of elements (or key-value pairs) remaining in a list, map, or set.
    uint32_t remaining_;
  };

  struct DecoderStatus {
    DecoderStatus(ProtocolState next_state) : next_state_(next_state), filter_status_{} {};
    DecoderStatus(ProtocolState next_state, FilterStatus filter_status)
        : next_state_(next_state), filter_status_(filter_status){};

    ProtocolState next_state_;
    absl::optional<FilterStatus> filter_status_;
  };

  // These functions map directly to the matching ProtocolState values. Each returns the next state
  // or ProtocolState::WaitForData if more data is required.
  DecoderStatus messageBegin(Buffer::Instance& buffer);
  DecoderStatus messageEnd(Buffer::Instance& buffer);
  DecoderStatus structBegin(Buffer::Instance& buffer);
  DecoderStatus structEnd(Buffer::Instance& buffer);
  DecoderStatus fieldBegin(Buffer::Instance& buffer);
  DecoderStatus fieldValue(Buffer::Instance& buffer);
  DecoderStatus fieldEnd(Buffer::Instance& buffer);
  DecoderStatus listBegin(Buffer::Instance& buffer);
  DecoderStatus listValue(Buffer::Instance& buffer);
  DecoderStatus listEnd(Buffer::Instance& buffer);
  DecoderStatus mapBegin(Buffer::Instance& buffer);
  DecoderStatus mapKey(Buffer::Instance& buffer);
  DecoderStatus mapValue(Buffer::Instance& buffer);
  DecoderStatus mapEnd(Buffer::Instance& buffer);
  DecoderStatus setBegin(Buffer::Instance& buffer);
  DecoderStatus setValue(Buffer::Instance& buffer);
  DecoderStatus setEnd(Buffer::Instance& buffer);

  // handleValue represents the generic Value state from the state machine documentation. It
  // returns either ProtocolState::WaitForData if more data is required or the next state. For
  // structs, lists, maps, or sets the return_state is pushed onto the stack and the next state is
  // based on elem_type. For primitive value types, return_state is returned as the next state
  // (unless WaitForData is returned).
  DecoderStatus handleValue(Buffer::Instance& buffer, FieldType elem_type,
                            ProtocolState return_state);

  // handleState delegates to the appropriate method based on state_.
  DecoderStatus handleState(Buffer::Instance& buffer);

  // Helper method to retrieve the current frame's return state and remove the frame from the
  // stack.
  ProtocolState popReturnState();

  Protocol& proto_;
  MessageMetadataSharedPtr metadata_;
  DecoderEventHandler& handler_;
  ProtocolState state_;
  std::vector<Frame> stack_;
};

typedef std::unique_ptr<DecoderStateMachine> DecoderStateMachinePtr;

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() {}

  /**
   * @return DecoderEventHandler& a new DecoderEventHandler for a message.
   */
  virtual DecoderEventHandler& newDecoderEventHandler() PURE;
};

/**
 * Decoder encapsulates a configured TransportPtr and ProtocolPtr.
 */
class Decoder : public Logger::Loggable<Logger::Id::thrift> {
public:
  Decoder(TransportPtr&& transport, ProtocolPtr&& protocol, DecoderCallbacks& callbacks);
  Decoder(TransportType transport_type, ProtocolType protocol_type, DecoderCallbacks& callbacks);

  /**
   * Drains data from the given buffer while executing a DecoderStateMachine over the data.
   *
   * @param data a Buffer containing Thrift protocol data
   * @param buffer_underflow bool set to true if more data is required to continue decoding
   * @return FilterStatus::StopIteration when waiting for filter continuation,
   *             Continue otherwise.
   * @throw EnvoyException on Thrift protocol errors
   */
  FilterStatus onData(Buffer::Instance& data, bool& buffer_underflow);

  TransportType transportType() { return transport_->type(); }
  ProtocolType protocolType() { return protocol_->type(); }

private:
  struct ActiveRequest {
    ActiveRequest(DecoderEventHandler& handler) : handler_(handler) {}

    DecoderEventHandler& handler_;
  };
  typedef std::unique_ptr<ActiveRequest> ActiveRequestPtr;

  void complete();

  TransportPtr transport_;
  ProtocolPtr protocol_;
  DecoderCallbacks& callbacks_;
  ActiveRequestPtr request_;
  MessageMetadataSharedPtr metadata_;
  DecoderStateMachinePtr state_machine_;
  bool frame_started_{false};
  bool frame_ended_{false};
};

typedef std::unique_ptr<Decoder> DecoderPtr;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
