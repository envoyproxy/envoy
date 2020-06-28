#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(OnDecodeStreamHeader)                                                                   \
  FUNCTION(OnDecodeStreamData)                                                                     \
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

struct ActiveStream {
  ActiveStream(StreamHandler& handler, MessageMetadataSharedPtr metadata, ContextSharedPtr context)
      : handler_(handler), metadata_(metadata), context_(context) {}
  ~ActiveStream() {
    metadata_.reset();
    context_.reset();
  }

  void onStreamDecoded() {
    ASSERT(metadata_ && context_);
    handler_.onStreamDecoded(metadata_, context_);
  }

  StreamHandler& handler_;
  MessageMetadataSharedPtr metadata_;
  ContextSharedPtr context_;
};

using ActiveStreamPtr = std::unique_ptr<ActiveStream>;

class DecoderStateMachine : public Logger::Loggable<Logger::Id::dubbo> {
public:
  class Delegate {
  public:
    virtual ~Delegate() = default;
    virtual ActiveStream* newStream(MessageMetadataSharedPtr metadata,
                                    ContextSharedPtr context) PURE;
    virtual void onHeartbeat(MessageMetadataSharedPtr metadata) PURE;
  };

  DecoderStateMachine(Protocol& protocol, Delegate& delegate)
      : protocol_(protocol), delegate_(delegate), state_(ProtocolState::OnDecodeStreamHeader) {}

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

private:
  struct DecoderStatus {
    DecoderStatus() = default;
    DecoderStatus(ProtocolState next_state) : next_state_(next_state){};
    DecoderStatus(ProtocolState next_state, FilterStatus filter_status)
        : next_state_(next_state), filter_status_(filter_status){};

    ProtocolState next_state_;
    absl::optional<FilterStatus> filter_status_;
  };

  // These functions map directly to the matching ProtocolState values. Each returns the next state
  // or ProtocolState::WaitForData if more data is required.
  DecoderStatus onDecodeStreamHeader(Buffer::Instance& buffer);
  DecoderStatus onDecodeStreamData(Buffer::Instance& buffer);

  // handleState delegates to the appropriate method based on state_.
  DecoderStatus handleState(Buffer::Instance& buffer);

  Protocol& protocol_;
  Delegate& delegate_;

  ProtocolState state_;
  ActiveStream* active_stream_{nullptr};
};

using DecoderStateMachinePtr = std::unique_ptr<DecoderStateMachine>;

class DecoderBase : public DecoderStateMachine::Delegate,
                    public Logger::Loggable<Logger::Id::dubbo> {
public:
  DecoderBase(Protocol& protocol);
  ~DecoderBase() override;

  /**
   * Drains data from the given buffer
   *
   * @param data a Buffer containing Dubbo protocol data
   * @throw EnvoyException on Dubbo protocol errors
   */
  FilterStatus onData(Buffer::Instance& data, bool& buffer_underflow);

  const Protocol& protocol() { return protocol_; }

  // It is assumed that all of the protocol parsing are stateless,
  // if there is a state of the need to provide the reset interface call here.
  void reset();

protected:
  void start();
  void complete();

  Protocol& protocol_;

  ActiveStreamPtr stream_;
  DecoderStateMachinePtr state_machine_;

  bool decode_started_{false};
};

/**
 * Decoder encapsulates a configured and ProtocolPtr and SerializationPtr.
 */
template <typename T> class Decoder : public DecoderBase {
public:
  Decoder(Protocol& protocol, T& callbacks) : DecoderBase(protocol), callbacks_(callbacks) {}

  ActiveStream* newStream(MessageMetadataSharedPtr metadata, ContextSharedPtr context) override {
    ASSERT(!stream_);
    stream_ = std::make_unique<ActiveStream>(callbacks_.newStream(), metadata, context);
    return stream_.get();
  }

  void onHeartbeat(MessageMetadataSharedPtr metadata) override { callbacks_.onHeartbeat(metadata); }

private:
  T& callbacks_;
};

class RequestDecoder : public Decoder<RequestDecoderCallbacks> {
public:
  RequestDecoder(Protocol& protocol, RequestDecoderCallbacks& callbacks)
      : Decoder(protocol, callbacks) {}
};

using RequestDecoderPtr = std::unique_ptr<RequestDecoder>;

class ResponseDecoder : public Decoder<ResponseDecoderCallbacks> {
public:
  ResponseDecoder(Protocol& protocol, ResponseDecoderCallbacks& callbacks)
      : Decoder(protocol, callbacks) {}
};

using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
