#include "source/extensions/upstreams/http/dynamic_modules/upstream_request.h"

#include <cstdint>
#include <memory>

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

// =============================================================================
// HttpTcpBridge
// =============================================================================

HttpTcpBridge::HttpTcpBridge(Router::UpstreamToDownstream* upstream_request,
                             Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                             BridgeConfigSharedPtr config)
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)),
      config_(std::move(config)) {
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);

  in_module_bridge_ =
      (*config_->on_bridge_new_)(config_->in_module_config_, static_cast<void*>(this));
  if (in_module_bridge_ == nullptr) {
    ENVOY_LOG(error, "dynamic module bridge creation returned nullptr");
  }
}

HttpTcpBridge::~HttpTcpBridge() {
  *local_reply_guard_ = false;
  if (in_module_bridge_ != nullptr) {
    (*config_->on_bridge_destroy_)(in_module_bridge_);
    in_module_bridge_ = nullptr;
  }
}

Envoy::Http::Status HttpTcpBridge::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) {
  if (in_module_bridge_ == nullptr) {
    return absl::InternalError("dynamic module bridge is null");
  }

  request_headers_ = &headers;
  downstream_complete_ = end_stream;

  // Initialize default response headers.
  response_headers_ = Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
      {{Envoy::Http::Headers::get().Status, "200"}});

  const auto status = (*config_->on_bridge_encode_headers_)(static_cast<void*>(this),
                                                            in_module_bridge_, end_stream);

  switch (status) {
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status_Continue:
    encoding_state_ = EncodingState::WaitingData;
    sendDataToUpstream(end_stream);
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status_StopAndBuffer:
    encoding_state_ = EncodingState::WaitingAllData;
    sendDataToUpstream(false);
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_headers_status_EndStream: {
    encoding_state_ = EncodingState::Done;
    // Cannot call sendLocalReply synchronously here because decodeData with end_stream=true can
    // trigger stream completion that destroys this bridge while encodeHeaders is still on the
    // call stack. Defer the entire local reply to the next event loop iteration.
    local_reply_pending_ = true;
    // Move response data into the lambda capture to avoid accessing member variables after the
    // bridge may have been destroyed during stream completion.
    auto headers = std::move(response_headers_);
    auto body = std::make_shared<Buffer::OwnedImpl>();
    body->move(response_body_);
    upstream_conn_data_->connection().dispatcher().post(
        [this, guard = local_reply_guard_, headers = std::move(headers), body]() mutable {
          if (!*guard) {
            return;
          }
          local_reply_pending_ = false;
          if (upstream_request_ == nullptr) {
            return;
          }
          response_headers_sent_ = true;
          if (body->length() > 0) {
            upstream_request_->decodeHeaders(std::move(headers), false);
            upstream_request_->decodeData(*body, true);
          } else {
            upstream_request_->decodeHeaders(std::move(headers), true);
          }
        });
    break;
  }
  }

  return Envoy::Http::okStatus();
}

void HttpTcpBridge::encodeData(Buffer::Instance& data, bool end_stream) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = end_stream;

  if (encoding_state_ == EncodingState::WaitingAllData) {
    // In buffered mode, accumulate data.
    request_buffer_.move(data);
    if (!end_stream) {
      return;
    }
    // On end_of_stream, pass the full accumulated buffer to the module.
  } else {
    // In streaming mode, pass the current chunk.
    request_buffer_.drain(request_buffer_.length());
    request_buffer_.move(data);
  }

  const auto status =
      (*config_->on_bridge_encode_data_)(static_cast<void*>(this), in_module_bridge_, end_stream);

  switch (status) {
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_Continue:
    sendDataToUpstream(end_stream);
    if (end_stream) {
      encoding_state_ = EncodingState::Done;
    }
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_EndStream:
    encoding_state_ = EncodingState::Done;
    sendLocalReply();
    break;
  }
}

void HttpTcpBridge::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = true;

  const auto status =
      (*config_->on_bridge_encode_trailers_)(static_cast<void*>(this), in_module_bridge_);

  switch (status) {
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_Continue:
    sendDataToUpstream(true);
    encoding_state_ = EncodingState::Done;
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_encode_data_status_EndStream:
    encoding_state_ = EncodingState::Done;
    sendLocalReply();
    break;
  }
}

void HttpTcpBridge::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void HttpTcpBridge::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush,
                                          "dynamic_module_bridge_reset_stream");
}

void HttpTcpBridge::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  if (in_module_bridge_ == nullptr || upstream_request_ == nullptr) {
    return;
  }

  response_buffer_ = &data;
  bytes_meter_->addWireBytesReceived(data.length());

  const auto status = (*config_->on_bridge_on_upstream_data_)(static_cast<void*>(this),
                                                              in_module_bridge_, end_stream);

  response_buffer_ = nullptr;

  switch (status) {
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status_Continue:
    // Drain the upstream read buffer. The module has already copied any needed data into
    // response_body_ via the ABI callbacks. Without draining, the connection would re-deliver
    // the same data on the next read event since it accumulates in the read buffer.
    data.drain(data.length());
    sendResponseToDownstream(false);
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status_StopAndBuffer:
    // Data stays in the upstream connection's read buffer and will accumulate.
    break;
  case envoy_dynamic_module_type_on_upstream_http_tcp_bridge_on_upstream_data_status_EndStream:
    data.drain(data.length());
    sendResponseToDownstream(true);
    break;
  }
}

void HttpTcpBridge::onEvent(Network::ConnectionEvent event) {
  if ((event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose) &&
      upstream_request_ != nullptr) {
    if (local_reply_pending_ || response_headers_sent_) {
      // A deferred local reply is pending or the response was already completed.
      return;
    }
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

void HttpTcpBridge::sendDataToUpstream(bool end_stream) {
  if (request_buffer_.length() == 0 && !end_stream) {
    return;
  }
  bytes_meter_->addWireBytesSent(request_buffer_.length());
  upstream_conn_data_->connection().write(request_buffer_, end_stream);
}

void HttpTcpBridge::sendResponseToDownstream(bool end_stream) {
  if (!response_headers_sent_) {
    response_headers_sent_ = true;
    upstream_request_->decodeHeaders(std::move(response_headers_), false);
  }

  // Latch whether trailers need to be sent before calling decodeData. When end_stream is true,
  // decodeData can trigger stream completion that destroys this bridge, so member variables
  // must not be accessed after the call.
  const bool has_trailers = end_stream && response_trailers_;

  if (response_body_.length() > 0 || end_stream) {
    Buffer::OwnedImpl local_body;
    local_body.move(response_body_);
    upstream_request_->decodeData(local_body, end_stream && !has_trailers);
  }

  if (has_trailers) {
    upstream_request_->decodeTrailers(std::move(response_trailers_));
  }
}

void HttpTcpBridge::sendLocalReply() {
  if (upstream_request_ == nullptr) {
    return;
  }
  if (!response_headers_sent_) {
    response_headers_sent_ = true;
    if (response_body_.length() > 0) {
      upstream_request_->decodeHeaders(std::move(response_headers_), false);
      // Move body to a local buffer. decodeData with end_stream=true can trigger stream
      // completion that destroys this bridge.
      Buffer::OwnedImpl local_body;
      local_body.move(response_body_);
      upstream_request_->decodeData(local_body, true);
    } else {
      upstream_request_->decodeHeaders(std::move(response_headers_), true);
    }
  }
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
