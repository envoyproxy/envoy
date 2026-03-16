#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

using OnBridgeConfigNewType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new);
using OnBridgeConfigDestroyType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_config_destroy);
using OnBridgeNewType = decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_new);
using OnBridgeEncodeHeadersType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_headers);
using OnBridgeEncodeDataType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_data);
using OnBridgeEncodeTrailersType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_encode_trailers);
using OnBridgeOnUpstreamDataType =
    decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_on_upstream_data);
using OnBridgeDestroyType = decltype(&envoy_dynamic_module_on_upstream_http_tcp_bridge_destroy);

/**
 * Configuration for the dynamic module upstream HTTP TCP bridge. This holds the loaded
 * dynamic module, resolved function pointers, and the in-module configuration.
 */
class BridgeConfig {
public:
  static absl::StatusOr<std::shared_ptr<BridgeConfig>>
  create(const std::string& bridge_name, const std::string& bridge_config,
         Envoy::Extensions::DynamicModules::DynamicModulePtr module);

  ~BridgeConfig();

  OnBridgeConfigNewType on_bridge_config_new_ = nullptr;
  OnBridgeConfigDestroyType on_bridge_config_destroy_ = nullptr;
  OnBridgeNewType on_bridge_new_ = nullptr;
  OnBridgeEncodeHeadersType on_bridge_encode_headers_ = nullptr;
  OnBridgeEncodeDataType on_bridge_encode_data_ = nullptr;
  OnBridgeEncodeTrailersType on_bridge_encode_trailers_ = nullptr;
  OnBridgeOnUpstreamDataType on_bridge_on_upstream_data_ = nullptr;
  OnBridgeDestroyType on_bridge_destroy_ = nullptr;

  envoy_dynamic_module_type_upstream_http_tcp_bridge_config_module_ptr in_module_config_ = nullptr;

private:
  BridgeConfig(Envoy::Extensions::DynamicModules::DynamicModulePtr module);

  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using BridgeConfigSharedPtr = std::shared_ptr<BridgeConfig>;

/**
 * TCP connection pool that wraps the standard TCP pool and creates HttpTcpBridge instances.
 */
class TcpConnPool : public Router::GenericConnPool, public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx,
              BridgeConfigSharedPtr config);
  ~TcpConnPool() override;

  // Router::GenericConnPool
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingStream() override;
  Upstream::HostDescriptionConstSharedPtr host() const override;
  bool valid() const override;

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  bool resetUpstreamHandleIfSet();

  absl::optional<Envoy::Upstream::TcpPoolData> conn_pool_data_;
  Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
  BridgeConfigSharedPtr config_;
};

/**
 * The upstream HTTP TCP bridge that delegates protocol transformation to a dynamic module.
 * This implements Router::GenericUpstream to receive HTTP from the UpstreamCodecFilter, and
 * Tcp::ConnectionPool::UpstreamCallbacks to receive TCP data from the upstream connection.
 *
 * The module controls the flow by calling explicit ABI callbacks (send_upstream_data,
 * send_response, send_response_headers, send_response_data, send_response_trailers) rather
 * than returning status codes from event hooks.
 */
class HttpTcpBridge : public Router::GenericUpstream,
                      public Envoy::Tcp::ConnectionPool::UpstreamCallbacks,
                      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  HttpTcpBridge(Router::UpstreamToDownstream* upstream_request,
                Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                BridgeConfigSharedPtr config);
  ~HttpTcpBridge() override;

  // Router::GenericUpstream
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                    bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  void enableTcpTunneling() override {}
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // Accessors for ABI callbacks.
  const Envoy::Http::RequestHeaderMap* requestHeaders() const { return request_headers_; }
  Buffer::Instance* requestBuffer() { return request_buffer_; }
  Buffer::Instance* responseBuffer() { return response_buffer_; }

  // Called by ABI callbacks to send data upstream.
  void sendUpstreamData(absl::string_view data, bool end_stream);

  // Called by ABI callbacks to send a complete local response downstream.
  void sendResponse(uint32_t status_code,
                    envoy_dynamic_module_type_module_http_header* headers_vector,
                    size_t headers_vector_size, absl::string_view body);

  // Called by ABI callbacks to send response headers downstream.
  void sendResponseHeaders(uint32_t status_code,
                           envoy_dynamic_module_type_module_http_header* headers_vector,
                           size_t headers_vector_size, bool end_stream);

  // Called by ABI callbacks to send response body data downstream.
  void sendResponseData(absl::string_view data, bool end_stream);

  // Called by ABI callbacks to send response trailers downstream.
  void sendResponseTrailers(envoy_dynamic_module_type_module_http_header* trailers_vector,
                            size_t trailers_vector_size);

private:
  Envoy::Http::ResponseHeaderMapPtr
  buildResponseHeaders(uint32_t status_code,
                       envoy_dynamic_module_type_module_http_header* headers_vector,
                       size_t headers_vector_size);

  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
  BridgeConfigSharedPtr config_;
  envoy_dynamic_module_type_upstream_http_tcp_bridge_module_ptr in_module_bridge_ = nullptr;

  bool downstream_complete_ = false;

  const Envoy::Http::RequestHeaderMap* request_headers_ = nullptr;
  Buffer::Instance* request_buffer_ = nullptr;
  Buffer::Instance* response_buffer_ = nullptr;

  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
};

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
