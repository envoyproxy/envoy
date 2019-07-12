#include "extensions/filters/listener/http_inspector/http_inspector.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

Config::Config(Stats::Scope& scope)
    : stats_{ALL_HTTP_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "http_inspector."))} {}

thread_local uint8_t Filter::buf_[Config::MAX_INSPECT_SIZE];

Filter::Filter(const ConfigSharedPtr config) : config_(config) {}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "http inspector: new connection accepted");

  Network::ConnectionSocket& socket = cb.socket();

  absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (transport_protocol != "" &&
      transport_protocol != TransportSockets::TransportSocketNames::get().RawBuffer) {
    ENVOY_LOG(debug, "http inspector: cannot inspect http protocol with transport socket {}",
              transport_protocol);
    return Network::FilterStatus::Continue;
  }

  ASSERT(file_event_ == nullptr);

  file_event_ =
      cb.dispatcher().createFileEvent(socket.ioHandle().fd(),
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
    parseHttpHeader(data, len);
  } else {
    done(false);
  }
}

void Filter::parseHttpHeader(const unsigned char* data, unsigned int len) {
  if (len >= HTTP2_CONNECTION_PREFACE_LEN &&
      !memcmp(data, HTTP2_CONNECTION_PREFACE, HTTP2_CONNECTION_PREFACE_LEN)) {
    ENVOY_LOG(trace, "http inspector: http2 connection preface found");
    protocol_ = "HTTP/2";
    config_->stats().http2_found_.inc();
    done(true);
  } else {
    unsigned int id = 0, sid = 0, spaces[2];
    while (id + 1 < len && (data[id] != '\r' || data[id + 1] != '\n')) {
      if (data[id] == ' ') {
        if (sid < 2) {
          spaces[sid++] = id;
        } else {
          sid = 0;
          break;
        }
      }
      id++;
    }

    if (sid != 2 || id + 1 == len) {
      ENVOY_LOG(trace, "http inspector: invalid http1x request line");
      done(false);
      return;
    }

    auto buf = reinterpret_cast<const char*>(data);
    absl::string_view method(buf, spaces[0]);
    // TODO(crazyxy): check request URI
    absl::string_view request_uri(buf + spaces[0] + 1, spaces[1] - spaces[0] - 1);
    absl::string_view http_version(buf + spaces[1] + 1, id - spaces[1] - 1);

    if (std::find(HTTP_METHODS.begin(), HTTP_METHODS.end(), method) == HTTP_METHODS.end() ||
        std::find(HTTP_PROTOCOLS.begin(), HTTP_PROTOCOLS.end(), http_version) ==
            HTTP_PROTOCOLS.end()) {
      ENVOY_LOG(trace, "http inspector: method: {} or protocol: {} not found", method,
                http_version);
      done(false);
      return;
    }

    ENVOY_LOG(trace, "http inspector: method: {}, request uri: {}, protocol: {}", method,
              request_uri, http_version);

    protocol_ = http_version;
    config_->stats().http1x_found_.inc();
    done(true);
  }
}

void Filter::done(bool success) {
  ENVOY_LOG(trace, "http inspector: done: {}", success);

  if (success) {
    absl::string_view protocol;
    if (protocol_ == "HTTP/1.0") {
      protocol = "http/1.0";
    } else if (protocol_ == "HTTP/1.1") {
      protocol = "http/1.1";
    } else {
      protocol = "h2";
    }

    cb_->socket().setRequestedApplicationProtocols({protocol});
  } else {
    config_->stats().http_not_found_.inc();
  }

  file_event_.reset();
  // Do not skip other listener filters.
  cb_->continueFilterChain(true);
}

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
