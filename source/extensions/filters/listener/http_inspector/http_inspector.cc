#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/http/headers.h"

#include "extensions/filters/listener/http_inspector/http_protocol_header.h"
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

Filter::Filter(const ConfigSharedPtr config) : config_(config) {}

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
    const size_t pos = data.find_first_of("\r\n");
    if (pos != absl::string_view::npos) {
      const absl::string_view request_line = data.substr(0, pos);
      const std::vector<absl::string_view> fields =
          absl::StrSplit(request_line, absl::MaxSplits(' ', 4));

      // Method SP Request-URI SP HTTP-Version
      if (fields.size() != 3) {
        ENVOY_LOG(trace, "http inspector: invalid http1x request line");
        // done(false);
        return ParseState::Error;
      }

      if (http1xMethods().count(fields[0]) == 0 || httpProtocols().count(fields[2]) == 0) {
        ENVOY_LOG(trace, "http inspector: method: {} or protocol: {} not valid", fields[0],
                  fields[2]);
        // done(false);
        return ParseState::Error;
      }

      ENVOY_LOG(trace, "http inspector: method: {}, request uri: {}, protocol: {}", fields[0],
                fields[1], fields[2]);

      protocol_ = fields[2];
      // done(true);
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

const absl::flat_hash_set<std::string>& Filter::httpProtocols() const {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>,
                         Http::Headers::get().ProtocolStrings.Http10String,
                         Http::Headers::get().ProtocolStrings.Http11String);
}

const absl::flat_hash_set<std::string>& Filter::http1xMethods() const {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>,
                         {HttpInspector::ExtendedHeader::get().MethodValues.Acl,
                          HttpInspector::ExtendedHeader::get().MethodValues.Baseline_Control,
                          HttpInspector::ExtendedHeader::get().MethodValues.Bind,
                          HttpInspector::ExtendedHeader::get().MethodValues.Checkin,
                          HttpInspector::ExtendedHeader::get().MethodValues.Checkout,
                          HttpInspector::ExtendedHeader::get().MethodValues.Connect,
                          HttpInspector::ExtendedHeader::get().MethodValues.Copy,
                          HttpInspector::ExtendedHeader::get().MethodValues.Delete,
                          HttpInspector::ExtendedHeader::get().MethodValues.Get,
                          HttpInspector::ExtendedHeader::get().MethodValues.Head,
                          HttpInspector::ExtendedHeader::get().MethodValues.Label,
                          HttpInspector::ExtendedHeader::get().MethodValues.Link,
                          HttpInspector::ExtendedHeader::get().MethodValues.Lock,
                          HttpInspector::ExtendedHeader::get().MethodValues.Merge,
                          HttpInspector::ExtendedHeader::get().MethodValues.Mkactivity,
                          HttpInspector::ExtendedHeader::get().MethodValues.Mkcalendar,
                          HttpInspector::ExtendedHeader::get().MethodValues.Mkcol,
                          HttpInspector::ExtendedHeader::get().MethodValues.Mkredirectref,
                          HttpInspector::ExtendedHeader::get().MethodValues.Mkworkspace,
                          HttpInspector::ExtendedHeader::get().MethodValues.Move,
                          HttpInspector::ExtendedHeader::get().MethodValues.Options,
                          HttpInspector::ExtendedHeader::get().MethodValues.Orderpatch,
                          HttpInspector::ExtendedHeader::get().MethodValues.Patch,
                          HttpInspector::ExtendedHeader::get().MethodValues.Post,
                          HttpInspector::ExtendedHeader::get().MethodValues.Proppatch,
                          HttpInspector::ExtendedHeader::get().MethodValues.Purge,
                          HttpInspector::ExtendedHeader::get().MethodValues.Put,
                          HttpInspector::ExtendedHeader::get().MethodValues.Rebind,
                          HttpInspector::ExtendedHeader::get().MethodValues.Report,
                          HttpInspector::ExtendedHeader::get().MethodValues.Search,
                          HttpInspector::ExtendedHeader::get().MethodValues.Trace,
                          HttpInspector::ExtendedHeader::get().MethodValues.Unbind,
                          HttpInspector::ExtendedHeader::get().MethodValues.Uncheckout,
                          HttpInspector::ExtendedHeader::get().MethodValues.Unlink,
                          HttpInspector::ExtendedHeader::get().MethodValues.Unlock,
                          HttpInspector::ExtendedHeader::get().MethodValues.Update,
                          HttpInspector::ExtendedHeader::get().MethodValues.Updateredirectref,
                          HttpInspector::ExtendedHeader::get().MethodValues.Version_Control});
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
