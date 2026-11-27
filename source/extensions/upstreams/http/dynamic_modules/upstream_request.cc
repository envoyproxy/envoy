#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

// =============================================================================
// BridgeConfig
// =============================================================================

BridgeConfig::BridgeConfig(Envoy::Extensions::DynamicModules::DynamicModulePtr module)
    : dynamic_module_(std::move(module)) {}

BridgeConfig::~BridgeConfig() {
  if (on_bridge_config_destroy_ && in_module_config_) {
    (*on_bridge_config_destroy_)(in_module_config_);
  }
}

absl::StatusOr<std::shared_ptr<BridgeConfig>>
BridgeConfig::create(const std::string& bridge_name, const std::string& bridge_config,
                     Envoy::Extensions::DynamicModules::DynamicModulePtr module) {
  auto config = std::shared_ptr<BridgeConfig>(new BridgeConfig(std::move(module)));

#define RESOLVE_OR_RETURN(field, symbol)                                                           \
  {                                                                                                \
    auto result = config->dynamic_module_->getFunctionPointer<decltype(field)>(symbol);            \
    RETURN_IF_NOT_OK_REF(result.status());                                                         \
    config->field = result.value();                                                                \
  }

  RESOLVE_OR_RETURN(on_bridge_config_new_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new");
  RESOLVE_OR_RETURN(on_bridge_config_destroy_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy");
  RESOLVE_OR_RETURN(on_bridge_new_, "envoy_dynamic_module_on_upstream_http_tcp_bridge_new");
  RESOLVE_OR_RETURN(on_bridge_encode_headers_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers");
  RESOLVE_OR_RETURN(on_bridge_encode_data_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data");
  RESOLVE_OR_RETURN(on_bridge_encode_trailers_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers");
  RESOLVE_OR_RETURN(on_bridge_on_upstream_data_,
                    "envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data");
  RESOLVE_OR_RETURN(on_bridge_destroy_, "envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy");

#undef RESOLVE_OR_RETURN

  const envoy_dynamic_module_type_envoy_buffer name_buf = {bridge_name.data(), bridge_name.size()};
  const envoy_dynamic_module_type_envoy_buffer config_buf = {bridge_config.data(),
                                                             bridge_config.size()};
  config->in_module_config_ =
      (*config->on_bridge_config_new_)(static_cast<void*>(config.get()), name_buf, config_buf);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("failed to initialize dynamic module bridge configuration");
  }

  return config;
}

// =============================================================================
// TcpConnPool
// =============================================================================

TcpConnPool::TcpConnPool(Upstream::HostConstSharedPtr host,
                         Upstream::ThreadLocalCluster& thread_local_cluster,
                         Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx,
                         BridgeConfigSharedPtr config)
    : config_(std::move(config)) {
  conn_pool_data_ = thread_local_cluster.tcpConnPool(host, priority, ctx);
}

TcpConnPool::~TcpConnPool() {
  ENVOY_BUG(upstream_handle_ == nullptr, "upstream_handle not null");
  resetUpstreamHandleIfSet();
}

void TcpConnPool::newStream(Router::GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  upstream_handle_ = conn_pool_data_.value().newConnection(*this);
}

bool TcpConnPool::cancelAnyPendingStream() { return resetUpstreamHandleIfSet(); }

Upstream::HostDescriptionConstSharedPtr TcpConnPool::host() const {
  return conn_pool_data_.value().host();
}

bool TcpConnPool::valid() const { return conn_pool_data_.has_value(); }

void TcpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                absl::string_view transport_failure_reason,
                                Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onPoolFailure(reason, transport_failure_reason, host);
}

void TcpConnPool::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream = std::make_unique<HttpTcpBridge>(&callbacks_->upstreamToDownstream(),
                                                  std::move(conn_data), config_);
  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.connectionInfoProvider(),
                          latched_conn.streamInfo(), {});
}

bool TcpConnPool::resetUpstreamHandleIfSet() {
  if (upstream_handle_) {
    upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
    upstream_handle_ = nullptr;
    return true;
  }
  return false;
}

namespace {

// How long a drained upstream connection may go without writing any of what the module sent before
// Envoy resets it. It matches the HTTP connection manager's default delayed close timeout, which
// also restarts on write progress.
constexpr std::chrono::milliseconds UpstreamConnectionDrainTimeout{1000};

// Holds an upstream connection the bridge closes while some data the module sent is still queued,
// since closing at once would cut the request short behind a FIN that makes it look whole. Holding
// the connection data keeps the pool from handing the connection to another request. The pool
// reports every close of the connection to the holder of its data, its own teardown included, so
// the drainer always reaches the close event that deletes it.
class UpstreamConnectionDrainer : public Envoy::Tcp::ConnectionPool::UpstreamCallbacks,
                                  public Event::DeferredDeletable,
                                  public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  explicit UpstreamConnectionDrainer(
      Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& connection_data)
      : connection_data_(std::move(connection_data)) {}

  // Starts the drain. The drainer owns itself until its close event hands it to the dispatcher.
  static void drain(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& connection_data) {
    std::make_unique<UpstreamConnectionDrainer>(std::move(connection_data)).release()->start();
  }

  // Tcp::ConnectionPool::UpstreamCallbacks
  // A connection closing with FlushWrite reads nothing more, so this only completes the interface.
  void onUpstreamData(Buffer::Instance& data, bool) override { data.drain(data.length()); }
  void onEvent(Network::ConnectionEvent event) override {
    if (event != Network::ConnectionEvent::LocalClose &&
        event != Network::ConnectionEvent::RemoteClose) {
      return;
    }
    drain_timer_->disableTimer();
    connection().dispatcher().deferredDelete(std::unique_ptr<Event::DeferredDeletable>(this));
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void start() {
    connection_data_->addUpstreamCallbacks(*this);
    // A reset rather than a FIN tells the upstream that the request it has is not whole.
    drain_timer_ = connection().dispatcher().createTimer([this]() {
      ENVOY_CONN_LOG(debug,
                     "dynamic module bridge resets an upstream connection whose queued data "
                     "was not written in time",
                     connection());
      connection().close(Network::ConnectionCloseType::AbortReset,
                         "dynamic_module_bridge_drain_timeout");
    });
    drain_timer_->enableTimer(UpstreamConnectionDrainTimeout);
    // Each write the socket takes restarts the bound. The callback outlives the drainer only on a
    // closed connection, which writes nothing more, and then it unregisters.
    connection().addBytesSentCallback(
        [drain_timer = std::weak_ptr<Event::Timer>(drain_timer_)](uint64_t) {
          const std::shared_ptr<Event::Timer> timer = drain_timer.lock();
          if (timer == nullptr) {
            return false;
          }
          timer->enableTimer(UpstreamConnectionDrainTimeout);
          return true;
        });
    // The close may report itself before returning, so nothing touches the drainer after it.
    connection().close(Network::ConnectionCloseType::FlushWrite, "dynamic_module_bridge_drained");
  }

  Network::ClientConnection& connection() { return connection_data_->connection(); }

  Envoy::Tcp::ConnectionPool::ConnectionDataPtr connection_data_;
  std::shared_ptr<Event::Timer> drain_timer_;
};

} // namespace

// =============================================================================
// HttpTcpBridge
// =============================================================================

HttpTcpBridge::HttpTcpBridge(Router::UpstreamToDownstream* upstream_request,
                             Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                             BridgeConfigSharedPtr config)
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)),
      config_(std::move(config)) {
  upstream_conn_data_->addUpstreamCallbacks(*this);

  in_module_bridge_ =
      (*config_->on_bridge_new_)(config_->in_module_config_, static_cast<void*>(this));
  if (in_module_bridge_ == nullptr) {
    ENVOY_LOG(error, "dynamic module bridge creation returned nullptr");
  }
}

HttpTcpBridge::~HttpTcpBridge() {
  // A terminal response is deferred until after the hook returns, so the bridge is never destroyed
  // while a module hook still holds a borrow to it.
  ASSERT(!in_module_hook_);
  if (in_module_bridge_ != nullptr) {
    (*config_->on_bridge_destroy_)(in_module_bridge_);
    in_module_bridge_ = nullptr;
  }
  if (upstream_connection_closed_) {
    return;
  }
  Network::ClientConnection& upstream_connection = upstream_conn_data_->connection();
  const Network::Connection::State state = upstream_connection.state();
  if (state == Network::Connection::State::Closed) {
    return;
  }
  // Only the module knows where its protocol's exchange ends, so the connection serves another
  // request only when the module enabled its reuse. The whole request must have reached this
  // bridge, since the upstream may still be waiting for the rest of a partial one. A half-closed
  // connection cannot carry another request, nor can one whose reads are disabled deliver the next
  // reply. One above its write high watermark would give the next request a flow control resume it
  // never paused for, and data the socket has not taken yet would count against the next request's
  // own unsent data. When all of these hold, the member destructor returns the connection to the
  // pool.
  const bool open = state == Network::Connection::State::Open;
  const bool reads_enabled = open && upstream_connection.readEnabled();
  const bool above_high_watermark = upstream_connection.aboveHighWatermark();
  const bool data_unsent = unsent_upstream_bytes_ != nullptr && *unsent_upstream_bytes_ > 0;
  if (upstream_connection_reuse_enabled_ && downstream_complete_ && !upstream_half_closed_ &&
      reads_enabled && !above_high_watermark && !data_unsent) {
    ENVOY_CONN_LOG(debug, "dynamic module bridge returns the upstream connection to the pool",
                   upstream_connection);
    return;
  }
  ENVOY_CONN_LOG(debug,
                 "dynamic module bridge closes the upstream connection, reuse enabled: {}, "
                 "request complete: {}, half-closed: {}, open: {}, reads enabled: {}, "
                 "above high watermark: {}, data unsent: {}",
                 upstream_connection, upstream_connection_reuse_enabled_, downstream_complete_,
                 upstream_half_closed_, open, reads_enabled, above_high_watermark, data_unsent);
  if (open && data_unsent) {
    UpstreamConnectionDrainer::drain(std::move(upstream_conn_data_));
    return;
  }
  // Releasing the connection before the close keeps the close from reaching this bridge or
  // counting as a connection destroyed with an active request. The pool defers deleting a client it
  // closes, so the reference outlives the release, and a draining pool closes the client itself.
  upstream_conn_data_.reset();
  if (upstream_connection.state() != Network::Connection::State::Closed) {
    upstream_connection.close(Network::ConnectionCloseType::NoFlush,
                              "dynamic_module_bridge_destroyed");
  }
}

Envoy::Http::Status HttpTcpBridge::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) {
  if (in_module_bridge_ == nullptr) {
    return absl::InternalError("dynamic module bridge is null");
  }

  request_headers_ = &headers;
  downstream_complete_ = end_stream;

  in_module_hook_ = true;
  (*config_->on_bridge_encode_headers_)(static_cast<void*>(this), in_module_bridge_, end_stream);
  in_module_hook_ = false;

  applyPendingResponse();
  return Envoy::Http::okStatus();
}

void HttpTcpBridge::encodeData(Buffer::Instance& data, bool end_stream) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = end_stream;

  // Move into the owned request buffer so the module reads from a stable copy whose lifetime is
  // tied to this object. Draining first scopes the buffer to the current call. The module is
  // expected to forward the data via send_upstream_data, which writes to the connection and drains
  // naturally.
  request_buffer_.drain(request_buffer_.length());
  request_buffer_.move(data);

  in_module_hook_ = true;
  (*config_->on_bridge_encode_data_)(static_cast<void*>(this), in_module_bridge_, end_stream);
  in_module_hook_ = false;

  // A terminal response requested by the module is applied here, which may destroy this object. Do
  // not access any member variables after this call.
  applyPendingResponse();
}

void HttpTcpBridge::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = true;

  in_module_hook_ = true;
  (*config_->on_bridge_encode_trailers_)(static_cast<void*>(this), in_module_bridge_);
  in_module_hook_ = false;

  applyPendingResponse();
}

void HttpTcpBridge::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void HttpTcpBridge::resetStream() {
  upstream_request_ = nullptr;
  // A reset rather than a FIN tells the upstream that the part of the request it has is not whole.
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::AbortReset,
                                          "dynamic_module_bridge_reset_stream");
}

void HttpTcpBridge::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    upstream_half_closed_ = true;
  }
  if (in_module_bridge_ == nullptr || upstream_request_ == nullptr) {
    return;
  }

  // Move data into the owned response buffer before calling the module. The module callback may
  // trigger downstream processing that re-enables upstream reads, causing a re-entrant
  // onUpstreamData call. Moving the data first ensures the connection's read buffer is empty,
  // preventing the same data from being delivered twice. The owned buffer is tied to this object so
  // the pointer never outlives its storage. Draining first scopes the buffer to the current call.
  response_buffer_.drain(response_buffer_.length());
  response_buffer_.move(data);
  bytes_meter_->addWireBytesReceived(response_buffer_.length());

  in_module_hook_ = true;
  (*config_->on_bridge_on_upstream_data_)(static_cast<void*>(this), in_module_bridge_, end_stream);
  in_module_hook_ = false;

  // A terminal response requested by the module is applied here, which may destroy this object. Do
  // not access any member variables after this call.
  applyPendingResponse();
}

void HttpTcpBridge::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::LocalClose &&
      event != Network::ConnectionEvent::RemoteClose) {
    return;
  }
  upstream_connection_closed_ = true;
  // The reset may destroy this bridge, so nothing touches a member after it.
  if (upstream_request_ != nullptr) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
  }
}

void HttpTcpBridge::onAboveWriteBufferHighWatermark() {
  if (upstream_request_) {
    upstream_request_->onAboveWriteBufferHighWatermark();
  }
}

void HttpTcpBridge::onBelowWriteBufferLowWatermark() {
  if (upstream_request_) {
    upstream_request_->onBelowWriteBufferLowWatermark();
  }
}

Envoy::Http::ResponseHeaderMapPtr
HttpTcpBridge::buildResponseHeaders(uint32_t status_code,
                                    envoy_dynamic_module_type_module_http_header* headers_vector,
                                    size_t headers_vector_size) {
  auto headers = Envoy::Http::ResponseHeaderMapImpl::create();
  headers->setStatus(status_code);
  if (headers_vector != nullptr) {
    for (size_t i = 0; i < headers_vector_size; i++) {
      const auto& header = headers_vector[i];
      const absl::string_view key(static_cast<const char*>(header.key_ptr), header.key_length);
      const absl::string_view value(static_cast<const char*>(header.value_ptr),
                                    header.value_length);
      headers->addCopy(Envoy::Http::LowerCaseString(key), value);
    }
  }
  return headers;
}

void HttpTcpBridge::sendUpstreamData(absl::string_view data, bool end_stream) {
  if (upstream_conn_data_ == nullptr) {
    return;
  }
  // The connection takes no data after its write side ended, so Envoy drops what the module sends
  // after it ended its upstream data.
  if (module_ended_upstream_data_) {
    ENVOY_LOG(debug, "dynamic module bridge drops upstream data sent after the module ended it");
    return;
  }
  Buffer::OwnedImpl buffer;
  if (!data.empty()) {
    buffer.add(data);
  }
  if (buffer.length() > 0 || end_stream) {
    if (end_stream) {
      module_ended_upstream_data_ = true;
      upstream_half_closed_ = true;
      upstream_conn_data_->connection().enableHalfClose(true);
    }
    bytes_meter_->addWireBytesSent(buffer.length());
    addUnsentUpstreamBytes(buffer.length());
    upstream_conn_data_->connection().write(buffer, end_stream);
  }
}

// The count charges every byte the socket takes to this bridge, which holds while the bridge is the
// connection's only writer, since reuse needs every byte sent first. A transport socket that writes
// bytes of its own, such as a PROXY protocol header, or an upstream network filter that changes the
// byte count makes it read up to that many bytes low, and the clamp keeps it from wrapping.
void HttpTcpBridge::addUnsentUpstreamBytes(uint64_t length) {
  if (length == 0) {
    return;
  }
  if (unsent_upstream_bytes_ == nullptr) {
    unsent_upstream_bytes_ = std::make_shared<uint64_t>(0);
    upstream_conn_data_->connection().addBytesSentCallback(
        [unsent_upstream_bytes =
             std::weak_ptr<uint64_t>(unsent_upstream_bytes_)](uint64_t bytes_sent) {
          const std::shared_ptr<uint64_t> unsent = unsent_upstream_bytes.lock();
          if (unsent == nullptr) {
            return false;
          }
          *unsent -= std::min(*unsent, bytes_sent);
          return true;
        });
  }
  *unsent_upstream_bytes_ += length;
}

void HttpTcpBridge::sendResponse(uint32_t status_code,
                                 envoy_dynamic_module_type_module_http_header* headers_vector,
                                 size_t headers_vector_size, absl::string_view body) {
  if (upstream_request_ == nullptr) {
    return;
  }
  auto headers = buildResponseHeaders(status_code, headers_vector, headers_vector_size);
  // A complete response ends the stream. Defer it when a hook is on the stack so the reset it
  // triggers cannot destroy this bridge under a live module borrow.
  if (in_module_hook_) {
    pending_response_ = PendingResponse::Response;
    pending_headers_ = std::move(headers);
    pending_body_.assign(body.data(), body.size());
    return;
  }
  if (!body.empty()) {
    upstream_request_->decodeHeaders(std::move(headers), false);
    Buffer::OwnedImpl body_buffer(body);
    upstream_request_->decodeData(body_buffer, true);
  } else {
    upstream_request_->decodeHeaders(std::move(headers), true);
  }
}

void HttpTcpBridge::sendResponseHeaders(
    uint32_t status_code, envoy_dynamic_module_type_module_http_header* headers_vector,
    size_t headers_vector_size, bool end_stream) {
  if (upstream_request_ == nullptr) {
    return;
  }
  auto headers = buildResponseHeaders(status_code, headers_vector, headers_vector_size);
  if (in_module_hook_ && end_stream) {
    pending_response_ = PendingResponse::Headers;
    pending_headers_ = std::move(headers);
    return;
  }
  upstream_request_->decodeHeaders(std::move(headers), end_stream);
}

void HttpTcpBridge::sendResponseData(absl::string_view data, bool end_stream) {
  if (upstream_request_ == nullptr) {
    return;
  }
  if (in_module_hook_ && end_stream) {
    pending_response_ = PendingResponse::Data;
    pending_body_.assign(data.data(), data.size());
    return;
  }
  Buffer::OwnedImpl buffer(data);
  upstream_request_->decodeData(buffer, end_stream);
}

void HttpTcpBridge::sendResponseTrailers(
    envoy_dynamic_module_type_module_http_header* trailers_vector, size_t trailers_vector_size) {
  if (upstream_request_ == nullptr) {
    return;
  }
  auto trailers = Envoy::Http::ResponseTrailerMapImpl::create();
  if (trailers_vector != nullptr) {
    for (size_t i = 0; i < trailers_vector_size; i++) {
      const auto& trailer = trailers_vector[i];
      const absl::string_view key(static_cast<const char*>(trailer.key_ptr), trailer.key_length);
      const absl::string_view value(static_cast<const char*>(trailer.value_ptr),
                                    trailer.value_length);
      trailers->addCopy(Envoy::Http::LowerCaseString(key), value);
    }
  }
  if (in_module_hook_) {
    pending_response_ = PendingResponse::Trailers;
    pending_trailers_ = std::move(trailers);
    return;
  }
  upstream_request_->decodeTrailers(std::move(trailers));
}

void HttpTcpBridge::applyPendingResponse() {
  if (pending_response_ == PendingResponse::None || upstream_request_ == nullptr) {
    return;
  }
  const PendingResponse kind = pending_response_;
  pending_response_ = PendingResponse::None;
  // Move the captured response into locals and capture the downstream handle, because applying a
  // terminal response may reset the stream and destroy this bridge.
  Router::UpstreamToDownstream* upstream_request = upstream_request_;
  Envoy::Http::ResponseHeaderMapPtr headers = std::move(pending_headers_);
  Envoy::Http::ResponseTrailerMapPtr trailers = std::move(pending_trailers_);
  Buffer::OwnedImpl body_buffer(pending_body_);
  switch (kind) {
  case PendingResponse::Response:
    if (!pending_body_.empty()) {
      upstream_request->decodeHeaders(std::move(headers), false);
      upstream_request->decodeData(body_buffer, true);
    } else {
      upstream_request->decodeHeaders(std::move(headers), true);
    }
    break;
  case PendingResponse::Headers:
    upstream_request->decodeHeaders(std::move(headers), true);
    break;
  case PendingResponse::Data:
    upstream_request->decodeData(body_buffer, true);
    break;
  case PendingResponse::Trailers:
    upstream_request->decodeTrailers(std::move(trailers));
    break;
  case PendingResponse::None:
    break;
  }
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
