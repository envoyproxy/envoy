#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/http/headers.h"

#include "extensions/transport_sockets/well_known_names.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

Config::Config(Stats::Scope& scope)
    : stats_{ALL_HTTP_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "http_inspector."))} {}

const absl::string_view Filter::HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
thread_local uint8_t Filter::buf_[Config::MAX_INSPECT_SIZE];

Filter::Filter(const ConfigSharedPtr config) : config_(config) {
  http_parser_init(&parser_, HTTP_REQUEST);
  parser_.data = this;
}

http_parser_settings Filter::settings_{
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    [](http_parser* parser) -> int {
      // The body was skipped and http_parser will call on_message_complete and reset the state to
      // the start state.
      static_cast<Filter*>(parser->data)->onMessageComplete();
      return 0;
    }, // on_message_complete
    nullptr,
    nullptr,
};

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "http inspector: new connection accepted");

  const Network::ConnectionSocket& socket = cb.socket();

  const absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (!transport_protocol.empty() &&
      transport_protocol != TransportSockets::TransportProtocolNames::get().RawBuffer) {
    ENVOY_LOG(trace, "http inspector: cannot inspect http protocol with transport socket {}",
              transport_protocol);
    return Network::FilterStatus::Continue;
  }

  cb_ = &cb;
  const ParseState parse_state = onRead();
  switch (parse_state) {
  case ParseState::Error:
    // As per discussion in https://github.com/envoyproxy/envoy/issues/7864
    // we don't add new enum in FilterStatus so we have to signal the caller
    // the new condition.
    cb.socket().close();
    return Network::FilterStatus::StopIteration;
  case ParseState::Done:
    return Network::FilterStatus::Continue;
  case ParseState::Continue:
    // do nothing but create the event
    ASSERT(file_event_ == nullptr);
    file_event_ = cb.dispatcher().createFileEvent(
        socket.ioHandle().fd(),
        [this](uint32_t events) {
          ENVOY_LOG(trace, "http inspector event: {}", events);
          // inspector is always peeking and can never determine EOF.
          // Use this event type to avoid listener timeout on the OS supporting
          // FileReadyType::Closed.
          bool end_stream = events & Event::FileReadyType::Closed;

          const ParseState parse_state = onRead();
          switch (parse_state) {
          case ParseState::Error:
            file_event_.reset();
            cb_->continueFilterChain(false);
            break;
          case ParseState::Done:
            file_event_.reset();
            // Do not skip following listener filters.
            cb_->continueFilterChain(true);
            break;
          case ParseState::Continue:
            if (end_stream) {
              // Parser fails to determine http but the end of stream is reached. Fallback to
              // non-http.
              done(false);
              file_event_.reset();
              cb_->continueFilterChain(true);
            }
            // do nothing but wait for the next event
            break;
          }
        },
        Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Closed);
    return Network::FilterStatus::StopIteration;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

ParseState Filter::onRead() {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Network::ConnectionSocket& socket = cb_->socket();
  const Api::SysCallSizeResult result =
      os_syscalls.recv(socket.ioHandle().fd(), buf_, Config::MAX_INSPECT_SIZE, MSG_PEEK);
  ENVOY_LOG(trace, "http inspector: recv: {}", result.rc_);
  if (result.rc_ == -1 && result.errno_ == EAGAIN) {
    return ParseState::Continue;
  } else if (result.rc_ < 0) {
    config_->stats().read_error_.inc();
    return ParseState::Error;
  }

  const auto parse_state =
      parseHttpHeader(absl::string_view(reinterpret_cast<const char*>(buf_), result.rc_));
  read_ = result.rc_;
  switch (parse_state) {
  case ParseState::Continue:
    // do nothing but wait for the next event
    return ParseState::Continue;
  case ParseState::Error:
    done(false);
    return ParseState::Done;
  case ParseState::Done:
    done(true);
    return ParseState::Done;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

ParseState Filter::parseHttpHeader(absl::string_view data) {
  const size_t len = std::min(data.length(), Filter::HTTP2_CONNECTION_PREFACE.length());
  if (Filter::HTTP2_CONNECTION_PREFACE.compare(0, len, data, 0, len) == 0) {
    if (data.length() < Filter::HTTP2_CONNECTION_PREFACE.length()) {
      return ParseState::Continue;
    }
    ENVOY_LOG(trace, "http inspector: http2 connection preface found");
    protocol_ = "HTTP/2";
    return ParseState::Done;
  } else {
    ssize_t rc =
        http_parser_execute(&parser_, &settings_, data.data() + read_, data.length() - read_);
    ENVOY_LOG(trace,
              "http inspector: http_parser parsed {} chars, error code: {}, parser state: {}", rc,
              HTTP_PARSER_ERRNO(&parser_), parser_.state);

    // Errors in parsing HTTP.
    if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED) {
      return ParseState::Error;
    }

    // state 18 is the s_start_req. When the header is parsed, the http_parser will reset the state
    // to s_start_req. state 42 is s_req_http_end
    if ((parser_.state == 18 && message_complete_) || parser_.state >= 42) {
      if (parser_.http_major == 1 && parser_.http_minor == 1) {
        protocol_ = Http::Headers::get().ProtocolStrings.Http11String;
      } else {
        // Set other HTTP protocols to HTTP/1.0
        protocol_ = Http::Headers::get().ProtocolStrings.Http10String;
      }
      return ParseState::Done;
    } else {
      return ParseState::Continue;
    }
  }
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "http inspector: done: {}", success);

  if (success) {
    absl::string_view protocol;
    if (protocol_ == Http::Headers::get().ProtocolStrings.Http10String) {
      config_->stats().http10_found_.inc();
      protocol = "http/1.0";
    } else if (protocol_ == Http::Headers::get().ProtocolStrings.Http11String) {
      config_->stats().http11_found_.inc();
      protocol = "http/1.1";
    } else {
      ASSERT(protocol_ == "HTTP/2");
      config_->stats().http2_found_.inc();
      // h2 HTTP/2 over TLS, h2c HTTP/2 over TCP
      // TODO(yxue): use detected protocol from http inspector and support h2c token in HCM
      protocol = "h2c";
    }

    cb_->socket().setRequestedApplicationProtocols({protocol});
  } else {
    config_->stats().http_not_found_.inc();
  }
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
