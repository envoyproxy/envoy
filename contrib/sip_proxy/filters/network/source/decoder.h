#pragma once

#include "source/common/buffer/buffer_impl.h"

#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

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
class DecoderStateMachine : public Logger::Loggable<Logger::Id::filter> {
public:
  DecoderStateMachine(MessageMetadataSharedPtr& metadata, DecoderEventHandler& handler)
      : metadata_(metadata), handler_(handler) {}

  /**
   * Consumes as much data from the configured Buffer as possible and executes
   * the decoding state machine.
   * Once the Done state is reached, further invocations of run return
   * immediately with Done.
   *
   * @param buffer a buffer containing the remaining data to be processed
   * @return State returns with State::Done
   */
  State run();

private:
  friend class SipDecoderTest;
  struct DecoderStatus {
    DecoderStatus(State next_state) : next_state_(next_state){};
    DecoderStatus(State next_state, FilterStatus filter_status)
        : next_state_(next_state), filter_status_(filter_status){};

    State next_state_;
    absl::optional<FilterStatus> filter_status_;
  };

  DecoderStatus transportBegin();
  DecoderStatus messageBegin();
  DecoderStatus messageEnd();
  DecoderStatus transportEnd();

  // handleState delegates to the appropriate method based on state_.
  DecoderStatus handleState();

  MessageMetadataSharedPtr metadata_;
  DecoderEventHandler& handler_;
};

using DecoderStateMachinePtr = std::unique_ptr<DecoderStateMachine>;

class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  /**
   * @return DecoderEventHandler& a new DecoderEventHandler for a message.
   */
  virtual DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) PURE;
  virtual std::shared_ptr<SipSettings> settings() const PURE;
};

/**
 * Decoder encapsulates a configured Transport and Protocol and provides the
 * ability to decode Sip messages.
 */
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
  FilterStatus onData(Buffer::Instance& data, bool continue_handling = false);

  std::shared_ptr<SipSettings> settings() { return callbacks_.settings(); };

  MessageMetadataSharedPtr metadata() { return metadata_; }

  void restore(MessageMetadataSharedPtr metadata, DecoderEventHandler& decoder_event_handler) {
    complete();
    metadata_ = metadata;
    request_ = std::make_unique<ActiveRequest>(decoder_event_handler);
    state_machine_ = std::make_unique<DecoderStateMachine>(metadata_, request_->handler_);
  }

  void complete();

private:
  friend class SipConnectionManagerTest;
  friend class SipDecoderTest;
  struct ActiveRequest {
    ActiveRequest(DecoderEventHandler& handler) : handler_(handler) {}

    DecoderEventHandler& handler_;
  };
  using ActiveRequestPtr = std::unique_ptr<ActiveRequest>;

  int reassemble(Buffer::Instance& data);

  /**
   * After the data reassembled, parse the data and handle them
   * @param data string
   * @param length actual length of data, data.length() may less
   *               than length when other data after data.
   */
  FilterStatus onDataReady(Buffer::Instance& data);

  int decode();

private:
  HeaderType currentHeader() { return current_header_; }
  size_t rawOffset() { return raw_offset_; }
  void setCurrentHeader(HeaderType data) { current_header_ = data; }

  bool isFirstVia() { return first_via_; }
  void setFirstVia(bool flag) { first_via_ = flag; }
  bool isFirstRoute() { return first_route_; }
  void setFirstRoute(bool flag) { first_route_ = flag; }
  bool isFirstRecordRoute() { return first_record_route_; }
  void setFirstRecordRoute(bool flag) { first_record_route_ = flag; }
  bool isFirstServiceRoute() { return first_service_route_; }
  void setFirstServiceRoute(bool flag) { first_service_route_ = flag; }

  auto sipHeaderType(absl::string_view sip_line);
  MsgType sipMsgType(absl::string_view top_line);
  MethodType sipMethod(absl::string_view top_line);

  static absl::string_view domain(absl::string_view sip_header, HeaderType header_type);
  static void getParamFromHeader(absl::string_view header, MessageMetadataSharedPtr metadata);

  HeaderType current_header_{HeaderType::TopLine};
  size_t raw_offset_{0};

  bool first_via_{true};
  bool first_route_{true};
  bool first_record_route_{true};
  bool first_service_route_{true};

  class MessageHandler;
  class HeaderHandler {
  public:
    HeaderHandler(MessageHandler& parent);
    virtual ~HeaderHandler() = default;

    virtual int processVia(absl::string_view& header);
    virtual int processContact(absl::string_view& header);
    virtual int processPath(absl::string_view& header);
    virtual int processRoute(absl::string_view& header);
    virtual int processRecordRoute(absl::string_view& header);
    virtual int processServiceRoute(absl::string_view& header);
    virtual int processWwwAuth(absl::string_view& header);
    virtual int processAuth(absl::string_view& header);
    virtual int processPCookieIPMap(absl::string_view& header);

    MessageMetadataSharedPtr metadata() { return parent_.metadata(); }

    HeaderType currentHeader() { return parent_.currentHeader(); }
    size_t rawOffset() { return parent_.rawOffset(); }
    bool isFirstVia() { return parent_.isFirstVia(); }
    bool isFirstRoute() { return parent_.isFirstRoute(); }
    bool isFirstRecordRoute() { return parent_.isFirstRecordRoute(); }
    bool isFirstServiceRoute() { return parent_.isFirstServiceRoute(); }
    void setFirstVia(bool flag) { parent_.setFirstVia(flag); }
    void setFirstRoute(bool flag) { parent_.setFirstRoute(flag); }
    void setFirstRecordRoute(bool flag) { parent_.setFirstRecordRoute(flag); }
    void setFirstServiceRoute(bool flag) { parent_.setFirstServiceRoute(flag); }

    MessageHandler& parent_;
  };

  class MessageHandler {
  public:
    MessageHandler(std::shared_ptr<HeaderHandler> handler, Decoder& parent)
        : parent_(parent), handler_(std::move(handler)) {}
    virtual ~MessageHandler() = default;

    virtual void parseHeader(HeaderType& type, absl::string_view& header) PURE;

    MessageMetadataSharedPtr metadata() { return parent_.metadata(); }
    HeaderType currentHeader() { return parent_.currentHeader(); }
    size_t rawOffset() { return parent_.rawOffset(); }
    bool isFirstVia() { return parent_.isFirstVia(); }
    bool isFirstRoute() { return parent_.isFirstRoute(); }
    bool isFirstRecordRoute() { return parent_.isFirstRecordRoute(); }
    bool isFirstServiceRoute() { return parent_.isFirstServiceRoute(); }
    void setFirstVia(bool flag) { parent_.setFirstVia(flag); }
    void setFirstRoute(bool flag) { parent_.setFirstRoute(flag); }
    void setFirstRecordRoute(bool flag) { parent_.setFirstRecordRoute(flag); }
    void setFirstServiceRoute(bool flag) { parent_.setFirstServiceRoute(flag); }

    Decoder& parent_;

  protected:
    std::shared_ptr<HeaderHandler> handler_;
  };

  class REGISTERHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
  };

  class INVITEHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
  };

  class OK200HeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
  };

  class GeneralHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
  };

  class SUBSCRIBEHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
  };

  class FAILURE4XXHeaderHandler : public HeaderHandler {
  public:
    using HeaderHandler::HeaderHandler;
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
  };

  // This is used to handle Other Message
  class OthersHandler : public MessageHandler {
  public:
    OthersHandler(Decoder& parent)
        : MessageHandler(std::make_shared<HeaderHandler>(*this), parent) {}
    ~OthersHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  class FAILURE4XXHandler : public MessageHandler {
  public:
    FAILURE4XXHandler(Decoder& parent)
        : MessageHandler(std::make_shared<HeaderHandler>(*this), parent) {}
    ~FAILURE4XXHandler() override = default;

    void parseHeader(HeaderType& type, absl::string_view& header) override;
  };

  class MessageFactory {
  public:
    static std::shared_ptr<MessageHandler> create(MethodType type, Decoder& parent);
  };

  DecoderCallbacks& callbacks_;
  ActiveRequestPtr request_;
  MessageMetadataSharedPtr metadata_;
  DecoderStateMachinePtr state_machine_;
};

using DecoderPtr = std::unique_ptr<Decoder>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
