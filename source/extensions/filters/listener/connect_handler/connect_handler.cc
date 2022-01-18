#include "source/extensions/filters/listener/connect_handler/connect_handler.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ConnectHandler {

Config::Config(Stats::Scope& scope)
    : stats_{ALL_CONNECT_HANDLER_STATS(POOL_COUNTER_PREFIX(scope, "connect_handler."))} {}

Filter::Filter(const ConfigSharedPtr config) : config_(config) {
  http_parser_init(&parser_, HTTP_REQUEST);
}

http_parser_settings Filter::settings_{
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

thread_local uint8_t Filter::buf_[Config::MAX_INSPECT_SIZE];

namespace {
  constexpr absl::string_view response_body = " 200 Connection Established\r\n\r\n";
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "connect handler: new connection accepted");

  const Network::ConnectionSocket& socket = cb.socket();

  const absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    ENVOY_LOG(trace, "connect handler: cannot inspect http protocol with transport socket {}",
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
    cb.socket().ioHandle().initializeFileEvent(
        cb.dispatcher(),
        [this](uint32_t events) {
          ASSERT(events == Event::FileReadyType::Read);
          ENVOY_LOG(trace, "connect handler event: {}", events);

          const ParseState parse_state = onRead();
          switch (parse_state) {
          case ParseState::Error:
            cb_->socket().ioHandle().resetFileEvents();
            cb_->continueFilterChain(false);
            break;
          case ParseState::Done:
            cb_->socket().ioHandle().resetFileEvents();
            // Do not skip following listener filters.
            cb_->continueFilterChain(true);
            break;
          case ParseState::Continue:
            // do nothing but wait for the next event
            break;
          }
        },
        Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
    return Network::FilterStatus::StopIteration;
  }
  ENVOY_BUG(false, "Unexpected ParseState value for connect handler");
}

ParseState Filter::onRead() {
  auto result = cb_->socket().ioHandle().recv(buf_, Config::MAX_INSPECT_SIZE, MSG_PEEK);
  ENVOY_LOG(trace, "connnect handler: recv: {}", result.return_value_);
  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      return ParseState::Continue;
    }
    config_->stats().read_error_.inc();
    return ParseState::Error;
  }

  // Remote closed
  if (result.return_value_ == 0) {
    return ParseState::Error;
  }
  return parseConnect(absl::string_view(reinterpret_cast<const char*>(buf_), result.return_value_));
}

ParseState Filter::parseConnect(absl::string_view data) {
    const size_t len = std::min(data.length(), HTTP1_CONNECT_PREFACE.length());
    if (HTTP1_CONNECT_PREFACE.compare(0, len, data, 0, len) == 0) {
        if (data.length() < HTTP1_CONNECT_PREFACE.length()) {
            return ParseState::Continue;
        }
        else {
            absl::string_view new_data = data.substr(parser_.nread);
            size_t pos = new_data.find("\r\n\r\n");
            if (pos != absl::string_view::npos) {
                new_data = new_data.substr(0, pos + 4);
                ssize_t rc = http_parser_execute(&parser_, &settings_, new_data.data(), new_data.length());
                ENVOY_LOG(trace, "connect handler: http_parser parsed {} chars, error code: {}", rc,
                    HTTP_PARSER_ERRNO(&parser_));

                // Errors in parsing HTTP.
                if (HTTP_PARSER_ERRNO(&parser_) != HPE_OK && HTTP_PARSER_ERRNO(&parser_) != HPE_PAUSED) {
                    ENVOY_LOG(trace, "connect handler: errors in parsing HTTP");
                    config_->stats().connect_not_found_.inc();
                    return ParseState::Done;
                }
                if (parser_.http_major == 1) {
                    if (parser_.http_minor == 0) {
                      protocol_ = Http::Headers::get().ProtocolStrings.Http10String;
                    }
                    else if (parser_.http_minor == 1) {
                      protocol_ = Http::Headers::get().ProtocolStrings.Http11String;
                    }
                } else {
                  ENVOY_LOG(trace, "connect:handler: unsupported http version: {}.{}", parser_.http_major, parser_.http_minor);
                  config_->stats().connect_not_found_.inc();
                  return ParseState::Done;
                }

                std::vector<absl::string_view> fields = absl::StrSplit(data, absl::ByAnyChar(" :"));

                // drain data from listener
                auto result = cb_->socket().ioHandle().recv(buf_, pos + 4, 0);
                
                if (!result.ok()) {
                  config_->stats().read_error_.inc();
                  return ParseState::Error;
                }
                // Remote closed
                if (result.return_value_ == 0) {
                  return ParseState::Error;
                }
                // terminate CONNECT request
                resp_buf_.add(protocol_);
                resp_buf_.add(response_body);
                result = cb_->socket().ioHandle().write(resp_buf_);
                if (!result.ok()) {
                  config_->stats().write_error_.inc();
                  return ParseState::Error;
                }
                // Remote closed
                if (result.return_value_ == 0) {
                  return ParseState::Error;
                }
                cb_->socket().setRequestedServerName(fields[1]);
                config_->stats().connect_found_.inc();
                return ParseState::Done;
            }
            else {
                return ParseState::Continue;
            }
        }
    }
    else {
        ENVOY_LOG(trace, "connect handler: no CONNECT request found");
        config_->stats().connect_not_found_.inc();
        return ParseState::Done;
    }
}
} // namespace ConnectHandler
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
