#pragma once

#include "envoy/buffer/buffer.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/logger.h"

#include "extensions/filters/network/sip_proxy/filters/filter.h"
#include "extensions/filters/network/sip_proxy/protocol.h"
#include "extensions/filters/network/sip_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(TransportBegin)                                                                         \
  FUNCTION(MessageBegin)                                                                           \
  FUNCTION(MessageEnd)                                                                             \
  FUNCTION(TransportEnd)                                                                           \
  FUNCTION(Done)

/**
 * ProtocolState represents a set of states used in a state machine to decode
 * Sip requests and responses.
 */
enum class State { ALL_PROTOCOL_STATES(GENERATE_ENUM) };

class StateNameValues {
public:
  static const std::string& name(State state) {
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
 * DecoderStateMachine is the Sip message state machine
 */
class DecoderStateMachine : public Logger::Loggable<Logger::Id::sip> {
public:
  DecoderStateMachine(MessageMetadataSharedPtr& metadata, DecoderEventHandler& handler)
      : metadata_(metadata), handler_(handler), state_(State::TransportBegin) {}

  /**
   * Consumes as much data from the configured Buffer as possible and executes
   * the decoding state machine. Returns ProtocolState::WaitForData if more data
   * is required to complete processing of a message. Returns
   * ProtocolState::Done when the end of a message is successfully processed.
   * Once the Done state is reached, further invocations of run return
   * immediately with Done.
   *
   * @param buffer a buffer containing the remaining data to be processed
   * @return ProtocolState returns with ProtocolState::WaitForData or
   * ProtocolState::Done
   * @throw Envoy Exception if thrown by the underlying Protocol
   */
  State run();

  /**
   * @return the current ProtocolState
   */
  State currentState() const { return state_; }

  /**
   * Set the current state. Used for testing only.
   */
  void setCurrentState(State state) { state_ = state; }

private:
  struct DecoderStatus {
    DecoderStatus(State next_state) : next_state_(next_state){};
    DecoderStatus(State next_state, FilterStatus filter_status)
        : next_state_(next_state), filter_status_(filter_status){};

    State next_state_;
    absl::optional<FilterStatus> filter_status_;
  };

  // These functions map directly to the matching ProtocolState values. Each
  // returns the next state or ProtocolState::WaitForData if more data is
  // required.
  DecoderStatus transportBegin();
  DecoderStatus messageBegin();
  DecoderStatus messageEnd();
  DecoderStatus transportEnd();

  // handleState delegates to the appropriate method based on state_.
  DecoderStatus handleState();

  MessageMetadataSharedPtr metadata_;
  DecoderEventHandler& handler_;
  State state_;
};

using DecoderStateMachinePtr = std::unique_ptr<DecoderStateMachine>;

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  /**
   * @return DecoderEventHandler& a new DecoderEventHandler for a message.
   */
  virtual DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) PURE;
};

/**
 * Decoder encapsulates a configured Transport and Protocol and provides the
 * ability to decode Sip messages.
 */
// niefei
class Decoder : public Logger::Loggable<Logger::Id::filter> {
public:
  Decoder(DecoderCallbacks& callbacks);

  /**
   * Drains data from the given buffer while executing a state machine over the
   * data.
   *
   * @param data a Buffer containing Sip protocol data
   * @return FilterStatus::StopIteration when waiting for filter continuation,
   *             Continue otherwise.
   * @throw EnvoyException on Sip protocol errors
   */
  FilterStatus onData(Buffer::Instance& data);

protected:
  MessageMetadataSharedPtr metadata() { return metadata_; }

private:
  struct ActiveRequest {
    ActiveRequest(DecoderEventHandler& handler) : handler_(handler) {}

    DecoderEventHandler& handler_;
  };
  using ActiveRequestPtr = std::unique_ptr<ActiveRequest>;

  void complete();

  int reassemble(Buffer::Instance& data);

  /**
   * After the data reassembled, parse the data and handle them
   * @param data string
   * @param length actual length of data, data.length() may less
   *               than length when other data after data.
   */
  FilterStatus onDataReady(Buffer::Instance& data);

  int decode();

  HeaderType currentHeader() { return current_header_; }
  size_t rawOffset() { return raw_offset_; }
  void setCurrentHeader(HeaderType data) { current_header_ = data; }

  bool isFirstVia() { return first_via_; }
  void setFirstVia(bool flag) { first_via_ = flag; }
  bool isFirstRoute() { return first_route_; }
  void setFirstRoute(bool flag) { first_route_ = flag; }

  auto sipHeaderType(absl::string_view sip_line);
  MsgType sipMsgType(absl::string_view top_line);
  MethodType sipMethod(absl::string_view top_line);

  static absl::string_view domain(absl::string_view sip_header, HeaderType header_type);

  int parseTopLine(absl::string_view& top_line);

  HeaderType current_header_{HeaderType::TopLine};
  size_t raw_offset_{0};

  bool first_via_{true};
  bool first_route_{true};

  class MessageHandler;
  class HeaderHandler {
  public:
    HeaderHandler(MessageHandler& parent);
    virtual ~HeaderHandler() = default;

    using HeaderProcessor =
        std::unordered_map<HeaderType,
                           std::function<int(HeaderHandler*, absl::string_view& header)>>;

    virtual int processVia(absl::string_view& header) {
      if (!isFirstVia()) {
        return 0;
      }

      setFirstVia(false);

      metadata()->setTransactionId(
          header.substr(header.find("branch=") + strlen("branch="),
                        header.find(";pep") - header.find("branch=") - strlen("branch=")));
      return 0;
    }

    virtual int processPath(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    };

    virtual int processEvent(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    };

    virtual int processRoute(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    }
    virtual int processContact(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    }
    virtual int processCseq(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    }
    virtual int processRecordRoute(absl::string_view& header) {
      UNREFERENCED_PARAMETER(header);
      return 0;
    }

    MessageMetadataSharedPtr metadata() { return parent_.metadata(); }

    HeaderType currentHeader() { return parent_.currentHeader(); }
    size_t rawOffset() { return parent_.rawOffset(); }
    bool isFirstVia() { return parent_.isFirstVia(); }
    bool isFirstRoute() { return parent_.isFirstRoute(); }
    void setFirstVia(bool flag) { parent_.setFirstVia(flag); }
    void setFirstRoute(bool flag) { parent_.setFirstRoute(flag); }

    MessageHandler& parent_;
    HeaderProcessor header_processors_;
  };

  class MessageHandler {
  public:
    MessageHandler(std::shared_ptr<HeaderHandler> handler, Decoder& parent)
        : handler_(std::move(handler)), parent_(parent) {}
    virtual ~MessageHandler() = default;

    virtual void parseHeader(HeaderType& type, absl::string_view& header) PURE;

    MessageMetadataSharedPtr metadata() { return parent_.metadata(); }
    HeaderType currentHeader() { return parent_.currentHeader(); }
    size_t rawOffset() { return parent_.rawOffset(); }
    bool isFirstVia() { return parent_.isFirstVia(); }
    bool isFirstRoute() { return parent_.isFirstRoute(); }
    void setFirstVia(bool flag) { parent_.setFirstVia(flag); }
    void setFirstRoute(bool flag) { parent_.setFirstRoute(flag); }

  protected:
    std::shared_ptr<HeaderHandler> handler_;
    Decoder& parent_;
  };

  class REGISTERHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
    int processVia(absl::string_view& header) override;
    int processRoute(absl::string_view& header) override;
    int processRecordRoute(absl::string_view& header) override;
    int processPath(absl::string_view& header) override;
  };

  class INVITEHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
    int processVia(absl::string_view& header) override;
    int processRoute(absl::string_view& header) override;
    int processRecordRoute(absl::string_view& header) override;
  };

  class OK200HeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
    int processCseq(absl::string_view& header) override;
    int processRecordRoute(absl::string_view& header) override;
    int processContact(absl::string_view& header) override;
  };

  class GeneralHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
    int processRoute(absl::string_view& header) override;
    int processVia(absl::string_view& header) override;
    int processContact(absl::string_view& header) override;
  };

  class SUBSCRIBEHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
    int processEvent(absl::string_view& header) override;
    int processVia(absl::string_view& header) override;
    int processRoute(absl::string_view& header) override;
    int processContact(absl::string_view& header) override;
  };

  class REGISTERHandler : public MessageHandler {
  public:
    REGISTERHandler(Decoder& parent)
        : MessageHandler(std::make_shared<REGISTERHeaderHandler>(*this), parent) {}
    ~REGISTERHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  class INVITEHandler : public MessageHandler {
  public:
    INVITEHandler(Decoder& parent)
        : MessageHandler(std::make_shared<INVITEHeaderHandler>(*this), parent) {}
    ~INVITEHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  class OK200Handler : public MessageHandler {
  public:
    OK200Handler(Decoder& parent)
        : MessageHandler(std::make_shared<OK200HeaderHandler>(*this), parent) {}
    ~OK200Handler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  // This is used to handle ACK/BYE/CANCEL
  class GeneralHandler : public MessageHandler {
  public:
    GeneralHandler(Decoder& parent)
        : MessageHandler(std::make_shared<GeneralHeaderHandler>(*this), parent) {}
    ~GeneralHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  class SUBSCRIBEHandler : public MessageHandler {
  public:
    SUBSCRIBEHandler(Decoder& parent)
        : MessageHandler(std::make_shared<SUBSCRIBEHeaderHandler>(*this), parent) {}
    ~SUBSCRIBEHandler() override = default;
    void parseHeader(HeaderType& type, absl::string_view& header) override;
    void setEventType(std::string_view value) {
      if (value == "reg") {
        event_type_ = EventType::REG;
      } else {
        event_type_ = EventType::OTHERS;
      }
    }
  private:
    enum class EventType {
      REG,
      OTHERS
    };

    EventType event_type_;
  };

  // This is used to handle Other Message
  class OthersHandler : public MessageHandler {
  public:
    OthersHandler(Decoder& parent)
        : MessageHandler(std::make_shared<HeaderHandler>(*this), parent) {}
    ~OthersHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override {
      if (type == HeaderType::Via) {
        handler_->processVia(header);
      }
    };
  };

  class MessageFactory {
  public:
    static std::shared_ptr<MessageHandler> create(MethodType type, Decoder& parent);
  };

  DecoderCallbacks& callbacks_;
  ActiveRequestPtr request_;
  MessageMetadataSharedPtr metadata_;
  DecoderStateMachinePtr state_machine_;
  bool start_new_message_{true};
};

using DecoderPtr = std::unique_ptr<Decoder>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
