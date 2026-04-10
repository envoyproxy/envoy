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
  upstream_conn_data_->addUpstreamCallbacks(*this);

  in_module_bridge_ =
      (*config_->on_bridge_new_)(config_->in_module_config_, static_cast<void*>(this));
  if (in_module_bridge_ == nullptr) {
    ENVOY_LOG(error, "dynamic module bridge creation returned nullptr");
  }
}

HttpTcpBridge::~HttpTcpBridge() {
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

  (*config_->on_bridge_encode_headers_)(static_cast<void*>(this), in_module_bridge_, end_stream);

  return Envoy::Http::okStatus();
}

void HttpTcpBridge::encodeData(Buffer::Instance& data, bool end_stream) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = end_stream;

  // Move into a local buffer so the module reads from a stable copy. The module is expected
  // to forward the data via send_upstream_data, which writes to the connection and drains
  // naturally.
  Buffer::OwnedImpl local_buffer;
  local_buffer.move(data);
  request_buffer_ = &local_buffer;

  // The module callback may trigger decodeData with end_stream=true (e.g., via sendResponse),
  // which can cause the router to destroy this object. Do not access any member variables after
  // this call.
  (*config_->on_bridge_encode_data_)(static_cast<void*>(this), in_module_bridge_, end_stream);
}

void HttpTcpBridge::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  if (in_module_bridge_ == nullptr) {
    return;
  }
  downstream_complete_ = true;

  (*config_->on_bridge_encode_trailers_)(static_cast<void*>(this), in_module_bridge_);
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

  // Move data into a local buffer before calling the module. The module callback may trigger
  // downstream processing that re-enables upstream reads, causing a re-entrant onUpstreamData
  // call. Moving the data first ensures the connection's read buffer is empty, preventing the
  // same data from being delivered twice.
  Buffer::OwnedImpl local_buffer;
  local_buffer.move(data);

  response_buffer_ = &local_buffer;
  bytes_meter_->addWireBytesReceived(local_buffer.length());

  // The module callback may trigger decodeData with end_stream=true, which can cause the router
  // to call resetStream() and ultimately destroy this object. Do not access any member variables
  // after this call.
  (*config_->on_bridge_on_upstream_data_)(static_cast<void*>(this), in_module_bridge_, end_stream);
}

void HttpTcpBridge::onEvent(Network::ConnectionEvent event) {
  if ((event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose) &&
      upstream_request_ != nullptr) {
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
  Buffer::OwnedImpl buffer;
  if (!data.empty()) {
    buffer.add(data);
  }
  if (buffer.length() > 0 || end_stream) {
    if (end_stream) {
      upstream_conn_data_->connection().enableHalfClose(true);
    }
    bytes_meter_->addWireBytesSent(buffer.length());
    upstream_conn_data_->connection().write(buffer, end_stream);
  }
}

void HttpTcpBridge::sendResponse(uint32_t status_code,
                                 envoy_dynamic_module_type_module_http_header* headers_vector,
                                 size_t headers_vector_size, absl::string_view body) {
  if (upstream_request_ == nullptr) {
    return;
  }
  auto headers = buildResponseHeaders(status_code, headers_vector, headers_vector_size);
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
  upstream_request_->decodeHeaders(std::move(headers), end_stream);
}

void HttpTcpBridge::sendResponseData(absl::string_view data, bool end_stream) {
  if (upstream_request_ == nullptr) {
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
  upstream_request_->decodeTrailers(std::move(trailers));
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
