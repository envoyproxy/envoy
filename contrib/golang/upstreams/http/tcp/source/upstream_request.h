#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "google/protobuf/any.pb.h"
#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/upstream_request.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"
#include "processor_state.h"
#include "processor_state.h"

#include "contrib/golang/common/dso/dso.h"

#include "contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha/golang.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

class TcpUpstream;

class FilterConfig;

class Filter;

struct HttpConfigInternal : httpConfig {
  std::weak_ptr<FilterConfig> config_;
  HttpConfigInternal(std::weak_ptr<FilterConfig> c) { config_ = c; }
  std::weak_ptr<FilterConfig> weakFilterConfig() { return config_; }
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Configuration for the HTTP golang extension filter.
 */
class FilterConfig : public std::enable_shared_from_this<FilterConfig>,
                     Logger::Loggable<Logger::Id::http> {
public:
  FilterConfig(const envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config proto_config,
   Dso::TcpUpstreamDsoPtr dso_lib);
  ~FilterConfig();

  const std::string& soId() const { return so_id_; }
  const std::string& soPath() const { return so_path_; }
  const std::string& pluginName() const { return plugin_name_; }
  uint64_t getConfigId() { return config_id_; };

  void newGoPluginConfig();

private:
  const std::string plugin_name_;
  const std::string so_id_;
  const std::string so_path_;
  const ProtobufWkt::Any plugin_config_;

  Dso::TcpUpstreamDsoPtr dso_lib_;
  uint64_t config_id_{0};
  // TODO(duxin40): use rwlock.
  Thread::MutexBasicLockable mutex_{};
  // filter level config is created in C++ side, and freed by Golang GC finalizer.
  HttpConfigInternal* config_{nullptr};
};

// Go code only touch the fields in httpRequest
class RequestInternal : public httpRequest {
public:
  RequestInternal(Filter& filter)
      : decoding_state_(filter, this), encoding_state_(filter, this) {
    configId = 0;
  }

  void setWeakFilter(std::weak_ptr<Filter> f) { filter_ = f; }
  std::weak_ptr<Filter> weakFilter() { return filter_; }

  DecodingProcessorState& decodingState() { return decoding_state_; }
  EncodingProcessorState& encodingState() { return encoding_state_; }

  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string strValue;

private:
  std::weak_ptr<Filter> filter_;

  // The state of the filter on both the encoding and decoding side.
  DecodingProcessorState decoding_state_;
  EncodingProcessorState encoding_state_;
};

class Filter : public std::enable_shared_from_this<Filter>,
                     Logger::Loggable<Logger::Id::http> {
public:
  Filter(FilterConfigSharedPtr config, const std::string cluster_name, const std::string route_name) : config_(config),
   req_(new RequestInternal(*this)), encoding_state_(req_->encodingState()), decoding_state_(req_->decodingState()),  cluster_name_(cluster_name), route_name_(route_name) {
    // req is used by go, so need to use raw memory and then it is safe to release at the gc
    // finalize phase of the go object.
    req_->plugin_name.data = config->pluginName().data();
    req_->plugin_name.len = config->pluginName().length();
  };

  bool initRequest();
  bool initResponse();

  bool isProcessingInGo() {
    return decoding_state_.isProcessingInGo() || encoding_state_.isProcessingInGo();
  }    

  CAPIStatus getHeader(ProcessorState& state, absl::string_view key, uint64_t* value_data, int* value_len);
  CAPIStatus copyHeaders(ProcessorState& state, GoString* go_strs, char* go_buf);
  CAPIStatus setRespHeader(ProcessorState& state, absl::string_view key, absl::string_view value, headerAction act);
  CAPIStatus copyBuffer(ProcessorState& state, Buffer::Instance* buffer, char* data);
  CAPIStatus drainBuffer(ProcessorState& state, Buffer::Instance* buffer, uint64_t length);
  CAPIStatus setBufferHelper(ProcessorState& state, Buffer::Instance* buffer, absl::string_view& value, bufferAction action);
  CAPIStatus getStringValue(int id, uint64_t* value_data, int* value_len);

  FilterConfigSharedPtr config_;

  RequestInternal* req_{nullptr};

  EncodingProcessorState& encoding_state_;
  DecodingProcessorState& decoding_state_;

  // store response header for http
  std::unique_ptr<Envoy::Http::ResponseHeaderMapImpl> resp_headers_{nullptr};

private:
  const std::string cluster_name_;
  const std::string route_name_;
};

// Wrapper HttpRequestInternal to DeferredDeletable.
// Since we want keep httpRequest at the top of the HttpRequestInternal,
// so, HttpRequestInternal can not inherit the virtual class DeferredDeletable.
class RequestInternalWrapper : public Envoy::Event::DeferredDeletable {
public:
  RequestInternalWrapper(RequestInternal* req) : req_(req) {}
  ~RequestInternalWrapper() override { delete req_; }

private:
  RequestInternal* req_;
};

class TcpConnPool : public Router::GenericConnPool,
                    public Envoy::Tcp::ConnectionPool::Callbacks,
                    public std::enable_shared_from_this<TcpConnPool>,
                    Logger::Loggable<Logger::Id::golang> {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx, const Protobuf::Message& config);
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    upstream_handle_ = conn_pool_data_.value().newConnection(*this);
  }

  bool cancelAnyPendingStream() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  Upstream::HostDescriptionConstSharedPtr host() const override {
    return conn_pool_data_.value().host();
  }

  bool valid() const override { return conn_pool_data_.has_value(); }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, transport_failure_reason, host);    
  }

  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  std::string plugin_name_{};
  absl::optional<Envoy::Upstream::TcpPoolData> conn_pool_data_;
  Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;

  Dso::TcpUpstreamDsoPtr dynamic_lib_;
  FilterConfigSharedPtr config_;
};

enum class EndStreamType {
	NotEndStream,
	EndStream,
};

enum class SendDataStatus {
  // Send data with upstream conn not half close.
	SendDataWithTunneling,
	// Send data with upstream conn half close.
	SendDataWithNotTunneling,
	// Not Send data.
	NotSendData,
};

enum class ReceiveDataStatus {
	// Continue to deal with further data.
	ReceiveDataContinue,
	// Finish dealing with data.
	ReceiveDataFinish,
	// Failure when dealing with data.
	ReceiveDataFailure,
};

enum class DestroyReason {
  Normal,
  Terminate,
};

enum class EnvoyValue {
  RouteName = 1,
  ClusterName,
};

enum class HttpStatusCode : uint64_t {
  Success = 200,
  UpstreamProtocolError = 500,
};

using FilterSharedPtr = std::shared_ptr<Filter>;

class TcpUpstream : public Router::GenericUpstream,
                    public Envoy::Tcp::ConnectionPool::UpstreamCallbacks,
                    public std::enable_shared_from_this<TcpUpstream>,
                    Logger::Loggable<Logger::Id::golang>  {
public:
  TcpUpstream(Router::UpstreamToDownstream* upstream_request,
              Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream, Dso::TcpUpstreamDsoPtr dynamic_lib,
              FilterConfigSharedPtr config);
  ~TcpUpstream() override;            

  // GenericUpstream
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  void encodeTrailers(const Envoy::Http::RequestTrailerMap&) override;
  void enableTcpTunneling() override {upstream_conn_data_->connection().enableHalfClose(true);};
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

  const Router::RouteEntry* route_entry_;

// private:
//   // return true when it is first inited.
//   bool initRequest();
//   bool initResponse();

private:
  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
  Buffer::OwnedImpl response_buffer_{};
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};

  Dso::TcpUpstreamDsoPtr dynamic_lib_;

  FilterConfigSharedPtr config_;

  FilterSharedPtr filter_;

  // RequestInternal* req_{nullptr};

  // EncodingProcessorState& encoding_state_;
  // DecodingProcessorState& decoding_state_;
};



} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
