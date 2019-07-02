#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/deserializer.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(OnTransportBegin)                                                                       \
  FUNCTION(OnTransportEnd)                                                                         \
  FUNCTION(OnMessageBegin)                                                                         \
  FUNCTION(OnMessageEnd)                                                                           \
  FUNCTION(OnTransferHeaderTo)                                                                     \
  FUNCTION(OnTransferBodyTo)                                                                       \
  FUNCTION(Done)

/**
 * ProtocolState represents a set of states used in a state machine to decode Dubbo requests
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

class DecoderStateMachine : public Logger::Loggable<Logger::Id::dubbo> {
public:
  DecoderStateMachine(Protocol& protocol, Deserializer& deserializer,
                      MessageMetadataSharedPtr& metadata, DecoderCallbacks& decoder_callbacks)
      : protocol_(protocol), deserializer_(deserializer), metadata_(metadata),
        decoder_callbacks_(decoder_callbacks), state_(ProtocolState::OnTransportBegin) {}

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
  struct DecoderStatus {
    DecoderStatus() = default;
    DecoderStatus(ProtocolState next_state) : next_state_(next_state){};
    DecoderStatus(ProtocolState next_state, Network::FilterStatus filter_status)
        : next_state_(next_state), filter_status_(filter_status){};

    ProtocolState next_state_;
    absl::optional<Network::FilterStatus> filter_status_;
  };

  // These functions map directly to the matching ProtocolState values. Each returns the next state
  // or ProtocolState::WaitForData if more data is required.
  DecoderStatus onTransportBegin(Buffer::Instance& buffer, Protocol::Context& context);
  DecoderStatus onTransportEnd();
  DecoderStatus onTransferHeaderTo(Buffer::Instance& buffer, size_t length);
  DecoderStatus onTransferBodyTo(Buffer::Instance& buffer, int32_t length);
  DecoderStatus onMessageBegin();
  DecoderStatus onMessageEnd(Buffer::Instance& buffer, int32_t message_size);

  // handleState delegates to the appropriate method based on state_.
  DecoderStatus handleState(Buffer::Instance& buffer);

  Protocol& protocol_;
  Deserializer& deserializer_;
  MessageMetadataSharedPtr metadata_;
  DecoderCallbacks& decoder_callbacks_;

  ProtocolState state_;
  Protocol::Context context_;

  DecoderEventHandler* handler_;
};

using DecoderStateMachinePtr = std::unique_ptr<DecoderStateMachine>;

/**
 * Decoder encapsulates a configured and ProtocolPtr and SerializationPtr.
 */
class Decoder : public Logger::Loggable<Logger::Id::dubbo> {
public:
  Decoder(Protocol& protocol, Deserializer& deserializer, DecoderCallbacks& decoder_callbacks);

  /**
   * Drains data from the given buffer
   *
   * @param data a Buffer containing Dubbo protocol data
   * @throw EnvoyException on Dubbo protocol errors
   */
  Network::FilterStatus onData(Buffer::Instance& data, bool& buffer_underflow);

  const Deserializer& serializer() { return deserializer_; }
  const Protocol& protocol() { return protocol_; }

private:
  void start();
  void complete();

  MessageMetadataSharedPtr metadata_;
  Deserializer& deserializer_;
  Protocol& protocol_;
  DecoderStateMachinePtr state_machine_;
  bool decode_started_ = false;
  DecoderCallbacks& decoder_callbacks_;
};

using DecoderPtr = std::unique_ptr<Decoder>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
