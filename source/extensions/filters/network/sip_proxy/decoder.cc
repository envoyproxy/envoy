#include <regex>

#include "envoy/common/exception.h"
#include "common/common/assert.h"
#include "common/common/macros.h"

#include "extensions/filters/network/sip_proxy/app_exception_impl.h"
#include "extensions/filters/network/sip_proxy/decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

DecoderStateMachine::DecoderStatus DecoderStateMachine::transportBegin() {
  return {State::MessageBegin, handler_.transportBegin(metadata_)};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::messageBegin() {
  return {State::MessageEnd, handler_.messageBegin(metadata_)};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::messageEnd() {
  return {State::TransportEnd, handler_.messageEnd()};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::transportEnd() {
  return {State::Done, handler_.transportEnd()};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleState() {
  switch (state_) {
  case State::TransportBegin:
    return transportBegin();
  case State::MessageBegin:
    return messageBegin();
  case State::MessageEnd:
    return messageEnd();
  case State::TransportEnd:
    return transportEnd();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

State DecoderStateMachine::run() {
  while (state_ != State::Done) {
    ENVOY_LOG(trace, "sip: state {}", StateNameValues::name(state_));

    DecoderStatus s = handleState();

    state_ = s.next_state_;

    ASSERT(s.filter_status_.has_value());
    if (s.filter_status_.value() == FilterStatus::StopIteration) {
      return State::StopIteration;
    }
  }

  return state_;
}

Decoder::Decoder(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

void Decoder::complete() {
  request_.reset();
  metadata_.reset();
  state_machine_ = nullptr;
  start_new_message_ = true;

  current_header_ = HeaderType::TopLine;
  raw_offset_ = 0;

  first_via_ = true;
  first_route_ = true;
}

FilterStatus Decoder::onData(Buffer::Instance& data) {
  ENVOY_LOG(debug, "sip: {} bytes available", data.length());

  reassemble(data);
  return FilterStatus::StopIteration;
}

int Decoder::reassemble(Buffer::Instance& data) {
  // ENVOY_LOG(trace, "received --> {}\n{}", data.length(), data.toString());

  Buffer::Instance& remaining_data = data;

  int ret = 0;
  size_t clen = 0;         // Content-Length value
  size_t full_msg_len = 0; // Length of the entire message

  while (remaining_data.length() != 0) {
    ssize_t content_pos = remaining_data.search("\n\r\n", strlen("\n\r\n"), 0);
    if (content_pos != -1) {
      // Get the Content-Length header value so that we can find
      // out the full message length.
      //
      content_pos += 3; // move to the line after the CRLF line.

      ssize_t content_length_start =
          remaining_data.search("Content-Length:", strlen("Content-Length:"), 0, content_pos);
      if (content_pos == -1) {
        break;
      }

      ssize_t content_length_end = remaining_data.search(
          "\r\n", strlen("\r\n"), content_length_start + strlen("Content-Length:"), content_pos);
      if (content_length_end == -1) {
        break;
      }

      char len[10]{}; // temporary storage
      remaining_data.copyOut(content_length_start + strlen("Content-Length:"),
                             content_length_end - content_length_start - strlen("Content-Length:"),
                             len);

      // Content-Length saved in clen.
      //
      clen = std::atoi(len);

      // Fail if Content-Length is less then zero
      //
      if (clen < 0) {
        break;
      }

      full_msg_len = content_pos + clen;
    }

    // Check for partial message received.
    //
    if ((full_msg_len == 0) || (full_msg_len > remaining_data.length())) {
      break;
    } else {
      // We have a full SIP message; put it on the dispatch queue.
      //
      Buffer::OwnedImpl message{};
      message.move(remaining_data, full_msg_len);
      auto status = onDataReady(message);
      message.drain(message.length());
      full_msg_len = 0;
      if (status != FilterStatus::StopIteration) {
        // break;
      }
    }
  } // End of while (remaining_data_len > 0)

  return ret;
}

FilterStatus Decoder::onDataReady(Buffer::Instance& data) {
  ENVOY_LOG(debug, "onDataReady {}\n{}", data.length(), data.toString());

  metadata_ = std::make_shared<MessageMetadata>(data.toString());

  decode();

  request_ = std::make_unique<ActiveRequest>(callbacks_.newDecoderEventHandler(metadata_));
  state_machine_ = std::make_unique<DecoderStateMachine>(metadata_, request_->handler_);
  State rv = state_machine_->run();

  if (rv == State::Done || rv == State::StopIteration) {
    complete();
  }

  return FilterStatus::StopIteration;
}

auto Decoder::sipHeaderType(absl::string_view sip_line) {
  static std::map<absl::string_view, HeaderType> sip_header_type_map{
      {"Call-ID", HeaderType::CallId},  {"Via", HeaderType::Via},
      {"To", HeaderType::To},           {"From", HeaderType::From},
      {"Contact", HeaderType::Contact}, {"Record-Route", HeaderType::RRoute},
      {"CSeq", HeaderType::Cseq},       {"Route", HeaderType::Route},
      {"Path", HeaderType::Path},       {"Event", HeaderType::Event},
      {"", HeaderType::Other}};

  auto header_type_str = sip_line.substr(0, sip_line.find_first_of(":"));
  if (auto result = sip_header_type_map.find(header_type_str);
      result != sip_header_type_map.end()) {
    return std::tuple<HeaderType, std::string_view>{
        result->second, sip_line.substr(sip_line.find_first_of(":") + 2)};
  } else {
    return std::tuple<HeaderType, std::string_view>{
        HeaderType::Other, sip_line.substr(sip_line.find_first_of(":") + 2)};
  }
}

MsgType Decoder::sipMsgType(absl::string_view top_line) {
  if (top_line.find("SIP/2.0 ") == absl::string_view::npos) {
    return MsgType::Request;
  } else {
    return MsgType::Response;
  }
}

MethodType Decoder::sipMethod(absl::string_view top_line) {
  if (top_line.find("INVITE") != absl::string_view::npos) {
    return MethodType::Invite;
  } else if (top_line.find("CANCEL") != absl::string_view::npos) {
    return MethodType::Cancel;
  } else if (top_line.find("REGISTER") != absl::string_view::npos) {
    return MethodType::Register;
  } else if (top_line.find("REFER") != absl::string_view::npos) {
    return MethodType::Refer;
  } else if (top_line.find("UPDATE") != absl::string_view::npos) {
    return MethodType::Update;
  } else if (top_line.find("SUBSCRIBE") != absl::string_view::npos) {
    return MethodType::Subscribe;
  } else if (top_line.find("NOTIFY") != absl::string_view::npos) {
    return MethodType::Notify;
  } else if (top_line.find("ACK") != absl::string_view::npos) {
    return MethodType::Ack;
  } else if (top_line.find("BYE") != absl::string_view::npos) {
    return MethodType::Bye;
  } else if (top_line.find("2.0 200") != absl::string_view::npos) {
    return MethodType::Ok200;
  } else {
    return MethodType::NullMethod;
  }
}

Decoder::HeaderHandler::HeaderHandler(MessageHandler& parent)
    : parent_(parent), header_processors_{
                           {HeaderType::Via, &HeaderHandler::processVia},
                           {HeaderType::Route, &HeaderHandler::processRoute},
                           {HeaderType::Contact, &HeaderHandler::processContact},
                           {HeaderType::Cseq, &HeaderHandler::processCseq},
                           {HeaderType::RRoute, &HeaderHandler::processRecordRoute},
                       } {}

//
// REGISTER Header Handler
//
int Decoder::REGISTERHeaderHandler::processPath(absl::string_view& header) {
  if (header.find(";ep=") != absl::string_view::npos) {
    // already Path have ep
    return 0;
  }

  metadata()->setInsertEPLocation(rawOffset() + header.length());
  return 0;
}

int Decoder::REGISTERHeaderHandler::processVia(absl::string_view& header) {
  if (!isFirstVia()) {
    return 0;
  }

  metadata()->setTransactionId(
      header.substr(header.find("branch=") + strlen("branch="),
                    header.find(";pep") - header.find("branch=") - strlen("branch=")));

  setFirstVia(false);
  return 0;
}

int Decoder::REGISTERHeaderHandler::processRoute(absl::string_view& header) {
  if (!isFirstRoute()) {
    return 0;
  }

  setFirstRoute(false);
  metadata()->setTopRoute(header);
  metadata()->setDomain(Decoder::domain(header, HeaderType::Route));
  return 0;
}

int Decoder::REGISTERHeaderHandler::processRecordRoute(absl::string_view& header) {
  if (header.find(";ep=") != absl::string_view::npos) {
    // already RR have ep
    return 0;
  }

  metadata()->setInsertEPLocation(rawOffset() + header.length());
  return 0;
}

//
// INVITE Header Handler
//
int Decoder::INVITEHeaderHandler::processVia(absl::string_view& header) {
  if (!isFirstVia()) {
    return 0;
  }

  metadata()->setTransactionId(
      header.substr(header.find("branch=") + strlen("branch="),
                    header.find(";pep") - header.find("branch=") - strlen("branch=")));

  setFirstVia(false);
  return 0;
}

int Decoder::INVITEHeaderHandler::processRoute(absl::string_view& header) {
  if (!isFirstRoute()) {
    return 0;
  }

  setFirstRoute(false);
  metadata()->setTopRoute(header);
  metadata()->setDomain(Decoder::domain(header, HeaderType::Route));
  return 0;
}

int Decoder::INVITEHeaderHandler::processRecordRoute(absl::string_view& header) {
  if (header.find(";ep=") != absl::string_view::npos) {
    // already RR have ep
    return 0;
  }

  metadata()->setInsertEPLocation(rawOffset() + header.length());
  return 0;
}

//
// 200 OK Header Handler
//
int Decoder::OK200HeaderHandler::processCseq(absl::string_view& header) {
  if (header.find("INVITE") != absl::string_view::npos) {
    metadata()->setRespMethodType(MethodType::Invite);
  }
  return 0;
}

int Decoder::OK200HeaderHandler::processRecordRoute(absl::string_view& header) {
  if (metadata()->respMethodType() != MethodType::Invite) {
    return 0;
  }

  if (header.find(";ep=") != absl::string_view::npos) {
    // already RR have ep
    return 0;
  }

  metadata()->setInsertEPLocation(rawOffset() + header.length());
  return 0;
}

int Decoder::OK200HeaderHandler::processContact(absl::string_view& header) {
  if (header.find("tag") != absl::string_view::npos) {
    return 0;
  }

  metadata()->setInsertTagLocation(rawOffset() + header.length());
  return 0;
}

//
// ACK/BYE/CANCEL Header Handler
//
int Decoder::GeneralHeaderHandler::processRoute(absl::string_view& header) {
  if (!isFirstRoute()) {
    return 0;
  }

  setFirstRoute(false);

  if (auto loc = header.find(";ep="); loc != absl::string_view::npos) {
    // already R have ep
    metadata()->setEP(header.substr(loc + 4));
  }
  metadata()->setTopRoute(header);
  metadata()->setDomain(Decoder::domain(header, HeaderType::Route));
  return 0;
}

int Decoder::GeneralHeaderHandler::processVia(absl::string_view& header) {
  if (!isFirstVia()) {
    return 0;
  }

  metadata()->setTransactionId(
      header.substr(header.find("branch=") + strlen("branch="),
                    header.find(";pep") - header.find("branch=") - strlen("branch=")));

  setFirstVia(false);
  return 0;
}

int Decoder::GeneralHeaderHandler::processContact(absl::string_view& header) {
  if (header.find("tag") != absl::string_view::npos) {
    // already contact have tag
    return 0;
  }

  metadata()->setInsertTagLocation(rawOffset() + header.length());
  return 0;
}

//
// SUBSCRIBE Header Handler
//
int Decoder::SUBSCRIBEHeaderHandler::processEvent(absl::string_view& header) {
  auto & parent = dynamic_cast<SUBSCRIBEHandler &>(this->parent_);
  parent.setEventType(header);
  return 0;
}

int Decoder::SUBSCRIBEHeaderHandler::processRoute(absl::string_view& header) {
  if (!isFirstRoute()) {
    return 0;
  }

  setFirstRoute(false);

  if (auto loc = header.find(";ep="); loc != absl::string_view::npos) {
    // already R have ep
    metadata()->setEP(header.substr(loc + 4));
  }
  metadata()->setTopRoute(header);
  metadata()->setDomain(Decoder::domain(header, HeaderType::Route));
  return 0;
}

int Decoder::SUBSCRIBEHeaderHandler::processVia(absl::string_view& header) {
  if (!isFirstVia()) {
    return 0;
  }

  metadata()->setTransactionId(
      header.substr(header.find("branch=") + strlen("branch="),
                    header.find(";pep") - header.find("branch=") - strlen("branch=")));

  setFirstVia(false);
  return 0;
}

int Decoder::SUBSCRIBEHeaderHandler::processContact(absl::string_view& header) {
  if (header.find("tag") != absl::string_view::npos) {
    // already contact have tag
    return 0;
  }

  metadata()->setInsertTagLocation(rawOffset() + header.length());
  return 0;
}

void Decoder::INVITEHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  if (type == HeaderType::Via) {
    handler_->processVia(header);
  } else if (type == HeaderType::Route) {
    handler_->processRoute(header);
  } else if (type == HeaderType::RRoute) {
    handler_->processRecordRoute(header);
  }
}

void Decoder::OK200Handler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Cseq:
    handler_->processCseq(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  default:
    break;
  }
}

void Decoder::GeneralHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  if (type == HeaderType::Route) {
    handler_->processRoute(header);
  }

  if (type == HeaderType::Via) {
    handler_->processVia(header);
  }

  if (type == HeaderType::Contact) {
    handler_->processContact(header);
  }
}

void Decoder::SUBSCRIBEHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  if (type == HeaderType::Event) {
    handler_->processEvent(header);
  }

  if (type == HeaderType::Route) {
    handler_->processRoute(header);
  }

  if (type == HeaderType::Via) {
    handler_->processVia(header);
  }

  if (type == HeaderType::Contact) {
    handler_->processContact(header);
  }
}

std::shared_ptr<Decoder::MessageHandler> Decoder::MessageFactory::create(MethodType type,
                                                                         Decoder& parent) {
  switch (type) {
  case MethodType::Invite:
    return std::make_shared<INVITEHandler>(parent);
  case MethodType::Ok200:
    return std::make_shared<OK200Handler>(parent);
  case MethodType::Ack:
  case MethodType::Bye:
  case MethodType::Cancel:
    return std::make_shared<GeneralHandler>(parent);
  default:
    return std::make_shared<OthersHandler>(parent);
  }
  return nullptr;
}

int Decoder::decode() {
  auto& metadata = metadata_;
  absl::string_view msg = absl::string_view(metadata->rawMsg());

  std::shared_ptr<MessageHandler> handler;

  while (!msg.empty()) {
    std::string::size_type crlf = msg.find("\r\n");
    if (crlf == absl::string_view::npos) {
      break;
    }

    if (current_header_ == HeaderType::TopLine) {
      // Sip Request Line
      absl::string_view sip_line = msg.substr(0, crlf);

      parseTopLine(sip_line);
      current_header_ = HeaderType::Other;

      handler = MessageFactory::create(metadata->methodType(), *this);
    } else {
      // Normal Header Line
      absl::string_view sip_line = msg.substr(0, crlf);
      auto [current_header, header_value] = sipHeaderType(sip_line);
      this->current_header_ = current_header;
      handler->parseHeader(current_header, sip_line);
    }

    msg = msg.substr(crlf + strlen("\r\n"));
    raw_offset_ += crlf + strlen("\r\n");

#if __cplusplus > 201703L
    if (msg.starts_with("\r\n")) {
#else
    if (msg[0] == '\r' && msg[1] == '\n') {
#endif
      break;
    }
  }

  if (!metadata->topRoute().has_value() && metadata->msgType() == MsgType::Request) {
    metadata->setDomain(domain(metadata->requestURI().value(), HeaderType::TopLine));
  }
  return 0;
}

int Decoder::parseTopLine(absl::string_view& top_line) {
  auto metadata = metadata_;
  metadata->setMsgType(sipMsgType(top_line));
  metadata->setMethodType(sipMethod(top_line));

  if (metadata->msgType() == MsgType::Request) {
    metadata->setRequestURI(top_line);
  }
  return 0;
}

absl::string_view Decoder::domain(absl::string_view sip_header, HeaderType header_type) {
  std::regex pattern;

  switch (header_type) {
  case HeaderType::TopLine:
    pattern = ".*@(.*?) .*";
    break;
  case HeaderType::Route: {
    pattern = ".*sip:(.*?)[:;].*";
    break;
  }
  default:
    return "";
  }

  std::match_results<absl::string_view::const_iterator> m;
  if (std::regex_match(sip_header.begin(), sip_header.end(), m, pattern)) {
    return absl::string_view{&*m[1].first, static_cast<size_t>(m[1].length())};
  } else {
    return "";
  }
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
