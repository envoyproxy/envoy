#include "contrib/sip_proxy/filters/network/source/decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

DecoderStateMachine::DecoderStatus DecoderStateMachine::transportBegin() {
  metadata_->setState(State::MessageBegin);
  return {State::MessageBegin, handler_.transportBegin(metadata_)};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::messageBegin() {
  metadata_->setState(State::MessageEnd);
  return {State::MessageEnd, handler_.messageBegin(metadata_)};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::messageEnd() {
  metadata_->setState(State::TransportEnd);
  return {State::TransportEnd, handler_.messageEnd()};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::transportEnd() {
  metadata_->setState(State::Done);
  return {State::Done, handler_.transportEnd()};
}

DecoderStateMachine::DecoderStatus DecoderStateMachine::handleState() {
  switch (metadata_->state()) {
  case State::TransportBegin:
    return transportBegin();
  case State::MessageBegin:
    return messageBegin();
  case State::MessageEnd:
    return messageEnd();
  case State::TransportEnd:
    return transportEnd();
  case State::HandleAffinity:
    return messageBegin();
  default:
    /**
     * test failed report "panic:     not reached" if reach here
     */
    PANIC("not reached");
  }
}

State DecoderStateMachine::run() {
  while (metadata_->state() != State::Done) {
    ENVOY_LOG(trace, "sip: state {}", StateNameValues::name(metadata_->state()));

    DecoderStatus s = handleState();

    ASSERT(s.filter_status_.has_value());
    if (s.filter_status_.value() == FilterStatus::StopIteration) {
      return State::StopIteration;
    }
  }

  return metadata_->state();
}

Decoder::Decoder(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

void Decoder::complete() {
  request_.reset();
  metadata_.reset();
  state_machine_ = nullptr;

  current_header_ = HeaderType::TopLine;
  raw_offset_ = 0;

  first_via_ = true;
  first_route_ = true;
  first_record_route_ = true;
  first_service_route_ = true;
}

FilterStatus Decoder::onData(Buffer::Instance& data, bool continue_handling) {
  if (continue_handling) {
    // means previous handling suspended, continue handling last request
    state_machine_->run();
    complete();
  } else {
    reassemble(data);
  }
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
      content_pos += strlen("\n\r\n"); // move to the line after the CRLF line.

      ssize_t content_length_start =
          remaining_data.search("Content-Length:", strlen("Content-Length:"), 0, content_pos);
      if (content_length_start == -1) {
        break;
      }

      ssize_t content_length_end = remaining_data.search(
          "\r\n", strlen("\r\n"), content_length_start + strlen("Content-Length:"), content_pos);

      // The "\n\r\n" is always included in remaining_data, so could not return -1
      // if (content_length_end == -1) {
      //   break;
      // }

      char len[10]{}; // temporary storage
      remaining_data.copyOut(content_length_start + strlen("Content-Length:"),
                             content_length_end - content_length_start - strlen("Content-Length:"),
                             len);

      clen = std::atoi(len);

      // atoi return value >= 0, could not < 0
      // if (clen < static_cast<size_t>(0)) {
      //   break;
      // }

      full_msg_len = content_pos + clen;
    }

    // Check for partial message received.
    if ((full_msg_len == 0) || (full_msg_len > remaining_data.length())) {
      break;
    } else {
      // We have a full SIP message; put it on the dispatch queue.
      Buffer::OwnedImpl message{};
      message.move(remaining_data, full_msg_len);
      onDataReady(message);

      // Even the handle of current sip message hasn't finished(async handle),
      // we use string of this buffer, so it can be drain safely.
      message.drain(message.length());
      full_msg_len = 0;
    }
  } // End of while (remaining_data_len > 0)

  return ret;
}

FilterStatus Decoder::onDataReady(Buffer::Instance& data) {
  ENVOY_LOG(debug, "SIP onDataReady {}\n{}", data.length(), data.toString());

  metadata_ = std::make_shared<MessageMetadata>(data.toString());

  decode();

  request_ = std::make_unique<ActiveRequest>(callbacks_.newDecoderEventHandler(metadata_));
  state_machine_ = std::make_unique<DecoderStateMachine>(metadata_, request_->handler_);
  State rv = state_machine_->run();

  if (rv == State::Done) {
    // complete();
  }
  complete();

  return FilterStatus::StopIteration;
}

auto Decoder::sipHeaderType(absl::string_view sip_line) {
  // XXX StringUtil::trim has poor performance
  auto delimiter_index = sip_line.find_first_of(':');
  auto header_type_str = sip_line.substr(0, delimiter_index);
  auto header_value = StringUtil::trim(sip_line.substr(delimiter_index + strlen(":")));
  return std::tuple<HeaderType, absl::string_view>{HeaderTypes::get().str2Header(header_type_str),
                                                   header_value};
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
  } else if (top_line.find("2.0 4") != absl::string_view::npos) {
    return MethodType::Failure4xx;
  } else {
    return MethodType::OtherMethod;
  }
}

Decoder::HeaderHandler::HeaderHandler(MessageHandler& parent) : parent_(parent) {}

int Decoder::HeaderHandler::processPath(absl::string_view& header) {
  metadata()->deleteInstipOperation(rawOffset(), header);
  metadata()->addEPOperation(rawOffset(), header, HeaderType::Path,
                             parent_.parent_.settings()->localServices());
  return 0;
}

int Decoder::HeaderHandler::processRoute(absl::string_view& header) {
  UNREFERENCED_PARAMETER(header);
  if (!isFirstRoute()) {
    return 0;
  }
  setFirstRoute(false);
  return 0;
}

int Decoder::HeaderHandler::processRecordRoute(absl::string_view& header) {
  if (!isFirstRecordRoute()) {
    return 0;
  }

  setFirstRecordRoute(false);

  metadata()->addEPOperation(rawOffset(), header, HeaderType::RRoute,
                             parent_.parent_.settings()->localServices());
  return 0;
}

int Decoder::HeaderHandler::processWwwAuth(absl::string_view& header) {
  metadata()->addOpaqueOperation(rawOffset(), header);
  return 0;
}

int Decoder::HeaderHandler::processAuth(absl::string_view& header) {
  auto loc = header.find("opaque=");
  if (loc == absl::string_view::npos) {
    return 0;
  }
  // has ""
  auto start = loc + strlen("opaque=\"");
  auto end = header.find('\"', start);
  if (end == absl::string_view::npos) {
    return 0;
  }

  metadata()->setOpaque(header.substr(start, end - start));
  return 0;
}

int Decoder::HeaderHandler::processPCookieIPMap(absl::string_view& header) {
  auto loc = header.find('=');
  if (loc == absl::string_view::npos) {
    return 0;
  }
  auto lskpmc =
      header.substr(header.find(": ") + strlen(": "), loc - header.find(": ") - strlen(": "));
  auto ip = header.substr(loc + 1, header.length() - loc - 1);

  metadata()->setPCookieIpMap(std::make_pair(std::string(lskpmc), std::string(ip)));
  metadata()->setOperation(Operation(OperationType::Delete, rawOffset(),
                                     DeleteOperationValue(header.length() + strlen("\r\n"))));
  return 0;
}

int Decoder::HeaderHandler::processContact(absl::string_view& header) {
  metadata()->deleteInstipOperation(rawOffset(), header);
  metadata()->addEPOperation(rawOffset(), header, HeaderType::Contact,
                             parent_.parent_.settings()->localServices());
  return 0;
}

int Decoder::HeaderHandler::processServiceRoute(absl::string_view& header) {
  if (!isFirstServiceRoute()) {
    return 0;
  }
  setFirstServiceRoute(false);

  metadata()->addEPOperation(rawOffset(), header, HeaderType::SRoute,
                             parent_.parent_.settings()->localServices());
  return 0;
}

void Decoder::REGISTERHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Route:
    handler_->processRoute(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::Path:
    handler_->processPath(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::SRoute:
    handler_->processServiceRoute(header);
    break;
  case HeaderType::Auth:
    handler_->processAuth(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::INVITEHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Route:
    handler_->processRoute(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::SRoute:
    handler_->processServiceRoute(header);
    break;
  case HeaderType::Path:
    handler_->processPath(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::OK200Handler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Path:
    handler_->processPath(header);
    break;
  case HeaderType::SRoute:
    handler_->processServiceRoute(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::GeneralHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Route:
    handler_->processRoute(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::Path:
    handler_->processPath(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::SUBSCRIBEHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Route:
    handler_->processRoute(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::FAILURE4XXHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::WAuth:
    handler_->processWwwAuth(header);
    break;
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

void Decoder::OthersHandler::parseHeader(HeaderType& type, absl::string_view& header) {
  switch (type) {
  case HeaderType::Via:
    handler_->processVia(header);
    break;
  case HeaderType::Contact:
    handler_->processContact(header);
    break;
  case HeaderType::Path:
    handler_->processPath(header);
    break;
  case HeaderType::RRoute:
    handler_->processRecordRoute(header);
    break;
  case HeaderType::SRoute:
    handler_->processServiceRoute(header);
    break;
  case HeaderType::PCookieIPMap:
    handler_->processPCookieIPMap(header);
    break;
  default:
    break;
  }
}

std::shared_ptr<Decoder::MessageHandler> Decoder::MessageFactory::create(MethodType type,
                                                                         Decoder& parent) {
  switch (type) {
  case MethodType::Invite:
    return std::make_shared<INVITEHandler>(parent);
  case MethodType::Ok200:
    return std::make_shared<OK200Handler>(parent);
  case MethodType::Register:
    return std::make_shared<REGISTERHandler>(parent);
  case MethodType::Subscribe:
    return std::make_shared<SUBSCRIBEHandler>(parent);
  case MethodType::Failure4xx:
    return std::make_shared<FAILURE4XXHandler>(parent);
  case MethodType::Ack:
  case MethodType::Bye:
  case MethodType::Cancel:
    return std::make_shared<GeneralHandler>(parent);
  default:
    return std::make_shared<OthersHandler>(parent);
  }
}

int Decoder::decode() {
  auto& metadata = metadata_;
  absl::string_view msg = absl::string_view(metadata->rawMsg());

  std::shared_ptr<MessageHandler> handler;

  while (!msg.empty()) {
    std::string::size_type crlf = msg.find("\r\n");
    // After message reassemble, this condition could not be true
    // if (crlf == absl::string_view::npos) {
    //   break;
    // }

    if (current_header_ == HeaderType::TopLine) {
      // Sip Request Line
      absl::string_view sip_line = msg.substr(0, crlf);

      metadata->addMsgHeader(HeaderType::TopLine, sip_line);
      metadata->setMsgType(sipMsgType(sip_line));
      metadata->setMethodType(sipMethod(sip_line));
      current_header_ = HeaderType::Other;

      handler = MessageFactory::create(metadata->methodType(), *this);

    } else {
      // Normal Header Line
      absl::string_view sip_line = msg.substr(0, crlf);
      auto [current_header, header_value] = sipHeaderType(sip_line);
      current_header_ = current_header;

      if (current_header == HeaderType::Other) {
        metadata->addMsgHeader(current_header, sip_line);
      } else {
        metadata->addMsgHeader(current_header, header_value);
      }
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
  return 0;
}

int Decoder::HeaderHandler::processVia(absl::string_view& header) {
  if (!isFirstVia()) {
    return 0;
  }

  metadata()->setTransactionId(header);

  setFirstVia(false);
  return 0;
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
