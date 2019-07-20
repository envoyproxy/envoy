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

Filter::Filter(const ConfigSharedPtr config) : config_(config) {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "http inspector: new connection accepted");

  const Network::ConnectionSocket& socket = cb.socket();

  const absl::string_view transport_protocol = socket.detectedTransportProtocol();
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
  const Network::ConnectionSocket& socket = cb_->socket();
  const Api::SysCallSizeResult result =
      os_syscalls.recv(socket.ioHandle().fd(), buf_, Config::MAX_INSPECT_SIZE, MSG_PEEK);
  ENVOY_LOG(trace, "http inspector: recv: {}", result.rc_);
  if (result.rc_ == -1 && result.errno_ == EAGAIN) {
    return;
  } else if (result.rc_ < 0) {
    config_->stats().read_error_.inc();
    done(false);
    return;
  }

  parseHttpHeader(absl::string_view(reinterpret_cast<const char*>(buf_), result.rc_));
}

void Filter::parseHttpHeader(absl::string_view data) {
  if (absl::StartsWith(data, Filter::HTTP2_CONNECTION_PREFACE)) {
    ENVOY_LOG(trace, "http inspector: http2 connection preface found");
    protocol_ = "HTTP/2";
    done(true);
  } else {
    const size_t pos = data.find_first_of("\r\n");

    // Cannot find \r or \n
    if (pos == absl::string_view::npos) {
      ENVOY_LOG(trace, "http inspector: no request line detected");
      return done(false);
    }

    const absl::string_view request_line = data.substr(0, pos);
    std::vector<absl::string_view> fields = absl::StrSplit(request_line, absl::MaxSplits(' ', 4));

    // Method SP Request-URI SP HTTP-Version
    if (fields.size() != 3) {
      ENVOY_LOG(trace, "http inspector: invalid http1x request line");
      return done(false);
    }

    if (httpProtocols().count(fields[2]) == 0) {
      ENVOY_LOG(trace, "http inspector: protocol: {} not valid", fields[2]);
      return done(false);
    }

    ENVOY_LOG(trace, "http inspector: method: {}, request uri: {}, protocol: {}", fields[0],
              fields[1], fields[2]);

    protocol_ = fields[2];
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

const absl::flat_hash_set<std::string>& Filter::httpProtocols() const {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>,
                         Http::Headers::get().ProtocolStrings.Http10String,
                         Http::Headers::get().ProtocolStrings.Http11String);
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
