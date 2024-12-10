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

#include "contrib/golang/common/dso/dso.h"

#include "contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha/golang.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

class ProcessorState;
class DecodingProcessorState;
class EncodingProcessorState;

class TcpUpstream;

class FilterConfig;

class Filter;

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
  * This describes the processor state.
*/
enum class FilterState {
  // Waiting header
  WaitingHeader,
  // Processing header in Go
  ProcessingHeader,
  // Waiting data
  WaitingData,
  // Waiting all data
  WaitingAllData,
  // Processing data in Go
  ProcessingData,
  // All done
  Done,
};
/**
  * An enum specific for Golang status.
*/
enum class TcpUpstreamStatus {
  /** 
  * Area of status: encodeHeaders, encodeData, onUpstreamData
  *
  * Used when you want to leave the current func area and continue further func. (when streaming, go side get each_data_piece, may be called multipled times)
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData, go side in encodeData will streaming get each_data_piece.
  * encodeData: streaming send data to upstream, go side get each_data_piece, may be called multipled times.
  * onUpstreamData: go side in onUpstreamData will get each_data_piece, pass data and headers to downstream streaming.
  */
  TcpUpstreamContinue,

  /** 
  * Area of status: encodeHeaders, encodeData, onUpstreamData
  *
  * Used when you want to buffer data.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData, encodeData will buffer whole data, go side in encodeData get whole data one-off.
  * encodeData: buffer further whole data, go side in encodeData get whole data one-off.(Be careful: This status MUST NOT be returned when end_stream is true.)
  * onUpstreamData: every data trigger will call go side, and go side get buffer data from start.(Be careful: This status MUST NOT be returned when end_stream is true.)
  */
  TcpUpstreamStopAndBuffer,

  /** Area of status: encodeHeaders, onUpstreamData
  *
  * Used when you want to send data to upstream in encodeHeaders, or send data to downstream in onUpstreamData.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: directly send data to upstream, and encodeData will not be called even when downstream_req has body.
  * onUpstreamData: send data and headers to downstream which means the whole resp to http is finished.
  */
  TcpUpstreamSendData,
};


/**
 * Configuration for the Tcp Upstream golang extension filter.
 */
class FilterConfig : httpConfig,
                     public std::enable_shared_from_this<FilterConfig>,
                     Logger::Loggable<Logger::Id::golang> {
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
};

class TcpConnPool : public Router::GenericConnPool,
                    public Envoy::Tcp::ConnectionPool::Callbacks,
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

using FilterSharedPtr = std::shared_ptr<Filter>;

class TcpUpstream : public Router::GenericUpstream,
                    public Envoy::Tcp::ConnectionPool::UpstreamCallbacks,
                    public httpRequest,
                    Logger::Loggable<Logger::Id::golang>  {
public:
  TcpUpstream(Router::UpstreamToDownstream* upstream_request,
              Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream, Dso::TcpUpstreamDsoPtr dynamic_lib,
              FilterConfigSharedPtr config);
  ~TcpUpstream() override;            

  enum class EndStreamType {
    NotEndStream,
    EndStream,
  };
  enum class HttpStatusCode : uint64_t {
    Success = 200,
  };
  enum class EnvoyValue {
    RouteName = 1,
    ClusterName,
  };

  void initResponse();

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

  void trySendProxyData(bool send_data_to_upstream, bool end_stream);
  void encodeDataGo(Buffer::Instance& data, bool end_stream);
  void sendDataToDownstream(Buffer::Instance& data, bool end_stream);

  CAPIStatus copyHeaders(GoString* go_strs, char* go_buf);
  CAPIStatus setRespHeader(absl::string_view key, absl::string_view value, headerAction act);
  CAPIStatus copyBuffer(Buffer::Instance* buffer, char* data);
  CAPIStatus drainBuffer(Buffer::Instance* buffer, uint64_t length);
  CAPIStatus setBufferHelper(Buffer::Instance* buffer, absl::string_view& value, bufferAction action);
  CAPIStatus getStringValue(int id, uint64_t* value_data, int* value_len);
  CAPIStatus setSelfHalfCloseForUpstreamConn(int enabled);

  // store response header for http
  const Envoy::Http::RequestOrResponseHeaderMap* req_headers_{nullptr};
  // store response header for http
  std::unique_ptr<Envoy::Http::ResponseHeaderMapImpl> resp_headers_{nullptr};

  const Router::RouteEntry* route_entry_;
  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string strValue;
  int state_;

  /* data buffer */
  // add data to state buffer
  virtual void addBufferData(Buffer::Instance& data) {
    if (data_buffer_ == nullptr) {
      data_buffer_ = std::make_unique<Buffer::OwnedImpl>();
    }
    data_buffer_->move(data);
  };

  std::string stateStr();
  FilterState filterState() const { return static_cast<FilterState>(state_); }
  void setFilterState(FilterState st) { state_ = static_cast<int>(st); }
  bool isProcessingInGo() {
    return filterState() == FilterState::ProcessingHeader || filterState() == FilterState::ProcessingData;
  }
  // get state buffer
  Buffer::Instance& getBufferData() { return *data_buffer_.get(); };
  bool isBufferDataEmpty() { return data_buffer_ == nullptr || data_buffer_->length() == 0; };
  void drainBufferData();

  void handleHeaderGolangStatus(TcpUpstreamStatus status);
  void handleDataGolangStatus(const TcpUpstreamStatus status, bool end_stream);

protected:
  Buffer::InstancePtr data_buffer_{nullptr};

private:
  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};

  Dso::TcpUpstreamDsoPtr dynamic_lib_;

  FilterConfigSharedPtr config_;

  bool upstream_conn_self_half_close_{false};

  bool already_send_resp_headers_{false};
};



} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
