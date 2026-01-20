#pragma once

#include <memory>

#include "envoy/http/codec.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/upstreams/http/dynamic_modules/bridge_config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

/**
 * HTTP-TCP bridge that delegates protocol transformation to a dynamic module.
 * This class receives HTTP requests, transforms them to TCP data via the module,
 * sends them upstream, and transforms TCP responses back to HTTP responses.
 */
class DynamicModuleHttpTcpBridge : public Router::GenericUpstream,
                                   public Envoy::Tcp::ConnectionPool::UpstreamCallbacks,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleHttpTcpBridge(DynamicModuleHttpTcpBridgeConfigSharedPtr config,
                             Router::UpstreamToDownstream* upstream_request,
                             Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream);
  ~DynamicModuleHttpTcpBridge() override;

  // Router::GenericUpstream
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                    bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override;
  void enableTcpTunneling() override { upstream_conn_data_->connection().enableHalfClose(true); }
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  // Envoy::Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // ABI callback accessors.
  size_t getRequestHeadersCount() const;
  bool getRequestHeader(size_t index, envoy_dynamic_module_type_envoy_buffer* key,
                        envoy_dynamic_module_type_envoy_buffer* value) const;
  bool getRequestHeaderValue(envoy_dynamic_module_type_module_buffer key,
                             envoy_dynamic_module_type_envoy_buffer* value) const;
  void getUpstreamBuffer(uintptr_t* buffer_ptr, size_t* length);
  void setUpstreamBuffer(envoy_dynamic_module_type_module_buffer data);
  void appendUpstreamBuffer(envoy_dynamic_module_type_module_buffer data);
  void getDownstreamBuffer(uintptr_t* buffer_ptr, size_t* length);
  void drainDownstreamBuffer(size_t length);
  bool setResponseHeader(envoy_dynamic_module_type_module_buffer key,
                         envoy_dynamic_module_type_module_buffer value);
  bool addResponseHeader(envoy_dynamic_module_type_module_buffer key,
                         envoy_dynamic_module_type_module_buffer value);
  void sendResponse(bool end_of_stream);
  void getRouteName(envoy_dynamic_module_type_envoy_buffer* result);
  void getClusterName(envoy_dynamic_module_type_envoy_buffer* result);

private:
  void initResponseHeaders();
  void sendDataToDownstream(Buffer::Instance& data, bool end_stream);

  const DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};

  // In-module bridge instance.
  envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr in_module_bridge_{nullptr};

  // Request headers stored for ABI access.
  const Envoy::Http::RequestHeaderMap* request_headers_{nullptr};

  // Response headers to send downstream.
  Envoy::Http::ResponseHeaderMapPtr response_headers_;
  bool response_headers_sent_{false};

  // Buffers for data transformation.
  Buffer::OwnedImpl upstream_buffer_;
  Buffer::Instance* downstream_buffer_{nullptr};

  // Route entry for cluster/route info.
  const Router::RouteEntry* route_entry_{nullptr};
};

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
