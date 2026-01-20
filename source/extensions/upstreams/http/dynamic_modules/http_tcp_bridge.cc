#include "source/extensions/upstreams/http/dynamic_modules/http_tcp_bridge.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

DynamicModuleHttpTcpBridge::DynamicModuleHttpTcpBridge(
    DynamicModuleHttpTcpBridgeConfigSharedPtr config,
    Router::UpstreamToDownstream* upstream_request,
    Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream)
    : config_(std::move(config)), upstream_request_(upstream_request),
      upstream_conn_data_(std::move(upstream)) {
  // Get route entry for cluster/route info.
  if (upstream_request_ != nullptr) {
    route_entry_ = upstream_request_->route().routeEntry();
  }

  // Enable half-close for proper TCP handling.
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);

  // Create the in-module bridge instance.
  in_module_bridge_ = config_->on_bridge_new_(config_->in_module_config_, this);
}

DynamicModuleHttpTcpBridge::~DynamicModuleHttpTcpBridge() {
  if (in_module_bridge_ != nullptr && config_->on_destroy_ != nullptr) {
    config_->on_destroy_(in_module_bridge_);
  }
}

void DynamicModuleHttpTcpBridge::initResponseHeaders() {
  if (response_headers_ == nullptr) {
    response_headers_ = Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>({});
  }
}

Envoy::Http::Status
DynamicModuleHttpTcpBridge::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                          bool end_stream) {
  ENVOY_LOG(debug, "dynamic module http-tcp bridge encode headers, size: {}, end_stream: {}",
            headers.size(), end_stream);

  // Initialize response headers in case the module sets them during encode.
  initResponseHeaders();

  // Store headers for ABI access.
  request_headers_ = &headers;

  // Clear the upstream buffer.
  upstream_buffer_.drain(upstream_buffer_.length());

  // Call the module to transform headers to TCP data.
  auto status = config_->on_encode_headers_(this, in_module_bridge_, end_stream);

  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_EndStream) {
    // Module wants to end the stream early (error case).
    ENVOY_LOG(warn, "dynamic module http-tcp bridge encode headers returned EndStream");
    sendDataToDownstream(upstream_buffer_, true);
    return Envoy::Http::okStatus();
  }

  // Send the buffer to upstream if it has data.
  if (upstream_buffer_.length() > 0 || end_stream) {
    upstream_conn_data_->connection().write(upstream_buffer_, end_stream);
  }

  return Envoy::Http::okStatus();
}

void DynamicModuleHttpTcpBridge::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "dynamic module http-tcp bridge encode data, length: {}, end_stream: {}",
            data.length(), end_stream);

  // Clear the upstream buffer and move incoming data to it.
  upstream_buffer_.drain(upstream_buffer_.length());
  upstream_buffer_.move(data);

  // Call the module to transform data.
  auto status = config_->on_encode_data_(this, in_module_bridge_, end_stream);

  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_EndStream) {
    // Module wants to end the stream early (error case).
    ENVOY_LOG(warn, "dynamic module http-tcp bridge encode data returned EndStream");
    sendDataToDownstream(upstream_buffer_, true);
    return;
  }

  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_StopAndBuffer) {
    // Module wants to buffer more data.
    if (end_stream) {
      // StopAndBuffer is not valid at end_stream.
      ENVOY_LOG(error,
                "dynamic module http-tcp bridge encode data returned StopAndBuffer at end_stream");
    }
    return;
  }

  // Send the buffer to upstream.
  if (upstream_buffer_.length() > 0 || end_stream) {
    upstream_conn_data_->connection().write(upstream_buffer_, end_stream);
  }
}

void DynamicModuleHttpTcpBridge::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  ENVOY_LOG(debug, "dynamic module http-tcp bridge encode trailers");
  // Trailers mark end of stream for TCP.
  Buffer::OwnedImpl empty_buffer;
  upstream_conn_data_->connection().write(empty_buffer, true);
}

void DynamicModuleHttpTcpBridge::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void DynamicModuleHttpTcpBridge::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void DynamicModuleHttpTcpBridge::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "dynamic module http-tcp bridge on upstream data, length: {}, end_stream: {}",
            data.length(), end_stream);

  // Initialize response headers if not already done.
  initResponseHeaders();

  // Store the downstream buffer for ABI access.
  downstream_buffer_ = &data;

  // Call the module to transform upstream TCP data to HTTP response.
  auto status = config_->on_upstream_data_(this, in_module_bridge_, end_stream);

  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_EndStream) {
    // Module wants to end the stream.
    end_stream = true;
  }

  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_StopAndBuffer) {
    // Module wants to buffer more data.
    if (end_stream) {
      ENVOY_LOG(error, "dynamic module http-tcp bridge on upstream data returned StopAndBuffer at "
                       "end_stream");
    }
    return;
  }

  // Continue means send the data downstream.
  if (status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_Continue ||
      status == envoy_dynamic_module_type_upstream_http_tcp_bridge_status_EndStream) {
    sendDataToDownstream(data, end_stream);
    data.drain(data.length());
  }
}

void DynamicModuleHttpTcpBridge::sendDataToDownstream(Buffer::Instance& data, bool end_stream) {
  if (!response_headers_sent_) {
    ENVOY_LOG(debug, "dynamic module http-tcp bridge send response headers to downstream");
    // Set default status if not set.
    if (!response_headers_->Status()) {
      response_headers_->setStatus(200);
    }
    upstream_request_->decodeHeaders(std::move(response_headers_), false);
    response_headers_sent_ = true;
  }

  upstream_request_->decodeData(data, end_stream);
}

void DynamicModuleHttpTcpBridge::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected && upstream_request_ != nullptr) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
  }
}

void DynamicModuleHttpTcpBridge::onAboveWriteBufferHighWatermark() {
  if (upstream_request_ != nullptr) {
    upstream_request_->onAboveWriteBufferHighWatermark();
  }
}

void DynamicModuleHttpTcpBridge::onBelowWriteBufferLowWatermark() {
  if (upstream_request_ != nullptr) {
    upstream_request_->onBelowWriteBufferLowWatermark();
  }
}

// ABI callback implementations.

size_t DynamicModuleHttpTcpBridge::getRequestHeadersCount() const {
  return request_headers_ != nullptr ? request_headers_->size() : 0;
}

bool DynamicModuleHttpTcpBridge::getRequestHeader(
    size_t index, envoy_dynamic_module_type_envoy_buffer* key,
    envoy_dynamic_module_type_envoy_buffer* value) const {
  if (request_headers_ == nullptr || index >= request_headers_->size()) {
    return false;
  }

  size_t current_index = 0;
  bool found = false;
  request_headers_->iterate(
      [&](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
        if (current_index == index) {
          key->ptr = header.key().getStringView().data();
          key->length = header.key().getStringView().size();
          value->ptr = header.value().getStringView().data();
          value->length = header.value().getStringView().size();
          found = true;
          return Envoy::Http::HeaderMap::Iterate::Break;
        }
        current_index++;
        return Envoy::Http::HeaderMap::Iterate::Continue;
      });

  return found;
}

bool DynamicModuleHttpTcpBridge::getRequestHeaderValue(
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* value) const {
  if (request_headers_ == nullptr) {
    return false;
  }

  absl::string_view key_view(key.ptr, key.length);
  const auto result = request_headers_->get(Envoy::Http::LowerCaseString(key_view));
  if (result.empty()) {
    return false;
  }

  value->ptr = result[0]->value().getStringView().data();
  value->length = result[0]->value().getStringView().size();
  return true;
}

void DynamicModuleHttpTcpBridge::getUpstreamBuffer(uintptr_t* buffer_ptr, size_t* length) {
  *buffer_ptr = reinterpret_cast<uintptr_t>(&upstream_buffer_);
  *length = upstream_buffer_.length();
}

void DynamicModuleHttpTcpBridge::setUpstreamBuffer(envoy_dynamic_module_type_module_buffer data) {
  upstream_buffer_.drain(upstream_buffer_.length());
  if (data.length > 0) {
    upstream_buffer_.add(data.ptr, data.length);
  }
}

void DynamicModuleHttpTcpBridge::appendUpstreamBuffer(
    envoy_dynamic_module_type_module_buffer data) {
  if (data.length > 0) {
    upstream_buffer_.add(data.ptr, data.length);
  }
}

void DynamicModuleHttpTcpBridge::getDownstreamBuffer(uintptr_t* buffer_ptr, size_t* length) {
  if (downstream_buffer_ != nullptr) {
    *buffer_ptr = reinterpret_cast<uintptr_t>(downstream_buffer_);
    *length = downstream_buffer_->length();
  } else {
    *buffer_ptr = 0;
    *length = 0;
  }
}

void DynamicModuleHttpTcpBridge::drainDownstreamBuffer(size_t length) {
  if (downstream_buffer_ != nullptr && length > 0) {
    downstream_buffer_->drain(length);
  }
}

bool DynamicModuleHttpTcpBridge::setResponseHeader(envoy_dynamic_module_type_module_buffer key,
                                                   envoy_dynamic_module_type_module_buffer value) {
  if (response_headers_sent_) {
    return false;
  }
  initResponseHeaders();
  response_headers_->setCopy(Envoy::Http::LowerCaseString(absl::string_view(key.ptr, key.length)),
                             absl::string_view(value.ptr, value.length));
  return true;
}

bool DynamicModuleHttpTcpBridge::addResponseHeader(envoy_dynamic_module_type_module_buffer key,
                                                   envoy_dynamic_module_type_module_buffer value) {
  if (response_headers_sent_) {
    return false;
  }
  initResponseHeaders();
  response_headers_->addCopy(Envoy::Http::LowerCaseString(absl::string_view(key.ptr, key.length)),
                             absl::string_view(value.ptr, value.length));
  return true;
}

void DynamicModuleHttpTcpBridge::sendResponse(bool end_of_stream) {
  if (downstream_buffer_ != nullptr) {
    sendDataToDownstream(*downstream_buffer_, end_of_stream);
    downstream_buffer_->drain(downstream_buffer_->length());
  } else {
    Buffer::OwnedImpl empty;
    sendDataToDownstream(empty, end_of_stream);
  }
}

void DynamicModuleHttpTcpBridge::getRouteName(envoy_dynamic_module_type_envoy_buffer* result) {
  if (upstream_request_ != nullptr && upstream_request_->route().virtualHost() != nullptr) {
    const auto& name = upstream_request_->route().virtualHost()->routeConfig().name();
    result->ptr = name.data();
    result->length = name.size();
  } else {
    result->ptr = nullptr;
    result->length = 0;
  }
}

void DynamicModuleHttpTcpBridge::getClusterName(envoy_dynamic_module_type_envoy_buffer* result) {
  if (route_entry_ != nullptr) {
    const auto& name = route_entry_->clusterName();
    result->ptr = name.data();
    result->length = name.size();
  } else {
    result->ptr = nullptr;
    result->length = 0;
  }
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
