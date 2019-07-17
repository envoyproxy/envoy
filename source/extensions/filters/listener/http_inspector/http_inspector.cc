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

  Network::ConnectionSocket& socket = cb.socket();

  absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (!transport_protocol.empty() &&
      transport_protocol != TransportSockets::TransportSocketNames::get().RawBuffer) {
    ENVOY_LOG(trace, "http inspector: cannot inspect http protocol with transport socket {}",
              transport_protocol);
    return Network::FilterStatus::Continue;
  }

  ASSERT(file_event_ == nullptr);

  file_event_ = cb.dispatcher().createFileEvent(
      socket.ioHandle().fd(),
      [this](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        onRead();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onRead() {
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.recv(cb_->socket().ioHandle().fd(), buf_, Config::MAX_INSPECT_SIZE, MSG_PEEK);
  ENVOY_LOG(trace, "http inspector: recv: {}", result.rc_);

  if (result.rc_ == -1 && result.errno_ == EAGAIN) {
    return;
  } else if (result.rc_ < 0) {
    config_->stats().read_error_.inc();
    done(false);
    return;
  }

  if (static_cast<uint64_t>(result.rc_) > read_) {
    const uint8_t* data = buf_ + read_;
    const size_t len = result.rc_ - read_;
    read_ = result.rc_;
    parseHttpHeader(absl::string_view(reinterpret_cast<const char*>(data), len));
  } else {
    done(false);
  }
}

void Filter::parseHttpHeader(absl::string_view data) {
  if (absl::StartsWith(data, Filter::HTTP2_CONNECTION_PREFACE)) {
    ENVOY_LOG(trace, "http inspector: http2 connection preface found");
    protocol_ = "HTTP/2";
    done(true);
  } else {
    unsigned int id, sid = 0, spaces[2];
    for (id = 0; id + 1 < data.length(); ++id) {
      // The end of the request line.
      if (data[id] == '\r' && data[id + 1] == '\n') {
        break;
      }

      if (data[id] == ' ') {
        if (sid < 2) {
          spaces[sid++] = id;
        } else {
          sid = 0;
          break;
        }
      }
    }

    if (sid != 2 || id + 1 == data.length()) {
      ENVOY_LOG(trace, "http inspector: invalid http1x request line");
      done(false);
      return;
    }

    absl::string_view method = data.substr(0, spaces[0]);
    // TODO(crazyxy): check request URI
    absl::string_view request_uri = data.substr(spaces[0] + 1, spaces[1] - spaces[0] - 1);
    absl::string_view http_version = data.substr(spaces[1] + 1, id - spaces[1] - 1);

    if (httpProtocols().count(http_version) == 0 || method.empty()) {
      ENVOY_LOG(trace, "http inspector: protocol: {} or method: {} not valid", http_version,
                method);
      done(false);
      return;
    }

    if (httpMethods().count(method) == 0) {
      ENVOY_LOG(trace, "http inspector: detect unknown method: {}", method);
    }

    ENVOY_LOG(trace, "http inspector: method: {}, request uri: {}, protocol: {}", method,
              request_uri, http_version);

    protocol_ = http_version;
    done(true);
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
      protocol = "h2";
    }

    cb_->socket().setRequestedApplicationProtocols({protocol});
  } else {
    config_->stats().http_not_found_.inc();
  }

  file_event_.reset();
  // Do not skip following listener filters.
  cb_->continueFilterChain(true);
}

const absl::flat_hash_set<std::string>& Filter::httpMethods() const {
  CONSTRUCT_ON_FIRST_USE(
      absl::flat_hash_set<std::string>,
      {Http::Headers::get().MethodValues.Connect, Http::Headers::get().MethodValues.Delete,
       Http::Headers::get().MethodValues.Get, Http::Headers::get().MethodValues.Head,
       Http::Headers::get().MethodValues.Post, Http::Headers::get().MethodValues.Put,
       Http::Headers::get().MethodValues.Options, Http::Headers::get().MethodValues.Trace});
}

const absl::flat_hash_set<std::string>& Filter::httpProtocols() const {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>,
                         Http::Headers::get().ProtocolStrings.Http10String,
                         Http::Headers::get().ProtocolStrings.Http11String);
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
