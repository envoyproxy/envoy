#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/network/dns.h"
#include "envoy/stats/sink.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/common/wasm/ext/redis_call.pb.h"
#include "source/extensions/common/wasm/plugin.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/network/common/redis/codec.h"

#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using proxy_wasm::BufferInterface;
using proxy_wasm::CloseType;
using proxy_wasm::ContextBase;
using proxy_wasm::Pairs;
using proxy_wasm::PairsWithStringValues;
using proxy_wasm::PluginBase;
using proxy_wasm::PluginHandleBase;
using proxy_wasm::SharedQueueDequeueToken;
using proxy_wasm::SharedQueueEnqueueToken;
using proxy_wasm::WasmBase;
using proxy_wasm::WasmBufferType;
using proxy_wasm::WasmHandleBase;
using proxy_wasm::WasmHeaderMapType;
using proxy_wasm::WasmResult;
using proxy_wasm::WasmStreamType;

using VmConfig = envoy::extensions::wasm::v3::VmConfig;
using CapabilityRestrictionConfig = envoy::extensions::wasm::v3::CapabilityRestrictionConfig;
using SanitizationConfig = envoy::extensions::wasm::v3::SanitizationConfig;
using GrpcService = envoy::config::core::v3::GrpcService;

class PluginHandle;
class Wasm;

using PluginBaseSharedPtr = std::shared_ptr<PluginBase>;
using PluginHandleBaseSharedPtr = std::shared_ptr<PluginHandleBase>;
using PluginHandleSharedPtr = std::shared_ptr<PluginHandle>;
using WasmHandleBaseSharedPtr = std::shared_ptr<WasmHandleBase>;

// Opaque context object.
class StorageObject {
public:
  virtual ~StorageObject() = default;
};

class Buffer : public proxy_wasm::BufferBase {
public:
  Buffer() = default;

  // proxy_wasm::BufferInterface
  size_t size() const override;
  WasmResult copyTo(WasmBase* wasm, size_t start, size_t length, uint64_t ptr_ptr,
                    uint64_t size_ptr) const override;
  WasmResult copyFrom(size_t start, size_t length, std::string_view data) override;

  // proxy_wasm::BufferBase
  void clear() override {
    proxy_wasm::BufferBase::clear();
    const_buffer_instance_ = nullptr;
    buffer_instance_ = nullptr;
  }
  Buffer* set(std::string_view data) {
    return static_cast<Buffer*>(proxy_wasm::BufferBase::set(data));
  }
  Buffer* set(std::unique_ptr<char[]> owned_data, uint32_t owned_data_size) {
    return static_cast<Buffer*>(
        proxy_wasm::BufferBase::set(std::move(owned_data), owned_data_size));
  }

  Buffer* set(::Envoy::Buffer::Instance* buffer_instance) {
    clear();
    buffer_instance_ = buffer_instance;
    const_buffer_instance_ = buffer_instance;
    return this;
  }
  Buffer* set(const ::Envoy::Buffer::Instance* buffer_instance) {
    clear();
    const_buffer_instance_ = buffer_instance;
    return this;
  }

private:
  const ::Envoy::Buffer::Instance* const_buffer_instance_{};
  ::Envoy::Buffer::Instance* buffer_instance_{};
};

// A context which will be the target of callbacks for a particular session
// e.g. a handler of a stream.
class Context : public proxy_wasm::ContextBase,
                public Logger::Loggable<Logger::Id::wasm>,
                public AccessLog::Instance,
                public Http::StreamFilter,
                public Network::ConnectionCallbacks,
                public Network::Filter,
                public Filters::Common::Expr::StreamActivation,
                public std::enable_shared_from_this<Context> {
public:
  Context();                                          // Testing.
  Context(Wasm* wasm);                                // Vm Context.
  Context(Wasm* wasm, const PluginSharedPtr& plugin); // Root Context.
  Context(Wasm* wasm, uint32_t root_context_id,
          PluginHandleSharedPtr plugin_handle); // Stream context.
  ~Context() override;

  WasmBase* wasm() const override;
  Wasm* envoyWasm() const;
  Plugin* plugin() const;
  Context* rootContext() const;
  Upstream::ClusterManager& clusterManager() const;

  // proxy_wasm::ContextBase
  void error(std::string_view message) override;

  // Retrieves the stream info associated with the request (a.k.a active stream).
  // It selects a value based on the following order: encoder callback, decoder
  // callback, log callback, network read filter callback, network write filter
  // callback. As long as any one of the callbacks is invoked, the value should be
  // available.
  const StreamInfo::StreamInfo* getConstRequestStreamInfo() const;
  StreamInfo::StreamInfo* getRequestStreamInfo() const;

  // Retrieves the connection object associated with the request (a.k.a active stream).
  // It selects a value based on the following order: encoder callback, decoder
  // callback. As long as any one of the callbacks is invoked, the value should be
  // available.
  const Network::Connection* getConnection() const;

  //
  // VM level down-calls into the Wasm code on Context(id == 0).
  //
  virtual bool validateConfiguration(std::string_view configuration,
                                     const std::shared_ptr<PluginBase>& plugin); // deprecated

  // Retargets/flushes this plugin instance's redis_call shared queue on (re)configuration.
  bool onConfigure(std::shared_ptr<PluginBase> plugin) override;

  // Resolves outstanding redis_call()s as Failure at shutdown, which may be well before
  // ~Context() runs.
  bool onDone() override;

  // AccessLog::Instance
  void log(const Formatter::Context& log_context, const StreamInfo::StreamInfo& info) override;

  uint32_t getLogLevel() override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  Network::FilterStatus onData(::Envoy::Buffer::Instance& data, bool end_stream) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(::Envoy::Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

  // proxy_wasm::ContextBase
  void onDownstreamConnectionClose(CloseType) override;
  void onUpstreamConnectionClose(CloseType) override;

  // Http::StreamFilterBase. Note: This calls onDone() in Wasm.
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(::Envoy::Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(::Envoy::Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void setEncoderFilterCallbacks(Envoy::Http::StreamEncoderFilterCallbacks& callbacks) override;

  // VM calls out to host.
  // proxy_wasm::ContextBase

  // General
  WasmResult log(uint32_t level, std::string_view message) override;
  uint64_t getCurrentTimeNanoseconds() override;
  uint64_t getMonotonicTimeNanoseconds() override;
  std::string_view getConfiguration() override;
  std::pair<uint32_t, std::string_view> getStatus() override;

  // State accessors
  WasmResult getProperty(std::string_view path, std::string* result) override;
  WasmResult setProperty(std::string_view path, std::string_view value) override;
  WasmResult setEnvoyFilterState(std::string_view path, std::string_view value,
                                 StreamInfo::FilterState::LifeSpan life_span);
  WasmResult declareProperty(std::string_view path,
                             Filters::Common::Expr::CelStatePrototypeConstPtr state_prototype);

  // Continue
  WasmResult continueStream(WasmStreamType stream_type) override;
  WasmResult closeStream(WasmStreamType stream_type) override;
  void failStream(WasmStreamType stream_type) override;
  WasmResult sendLocalResponse(uint32_t response_code, std::string_view body_text,
                               Pairs additional_headers, uint32_t grpc_status,
                               std::string_view details) override;
  void clearRouteCache() override {
    if (decoder_callbacks_ && decoder_callbacks_->downstreamCallbacks()) {
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }
  }

  // Header/Trailer/Metadata Maps
  WasmResult addHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                               std::string_view value) override;
  WasmResult getHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                               std::string_view* value) override;
  WasmResult getHeaderMapPairs(WasmHeaderMapType type, Pairs* result) override;
  WasmResult setHeaderMapPairs(WasmHeaderMapType type, const Pairs& pairs) override;

  WasmResult removeHeaderMapValue(WasmHeaderMapType type, std::string_view key) override;
  WasmResult replaceHeaderMapValue(WasmHeaderMapType type, std::string_view key,
                                   std::string_view value) override;

  WasmResult getHeaderMapSize(WasmHeaderMapType type, uint32_t* size) override;

  // Buffer
  BufferInterface* getBuffer(WasmBufferType type) override;
  // TODO: use stream_type.
  bool endOfStream(WasmStreamType /* stream_type */) override { return end_of_stream_; }

  // HTTP
  WasmResult httpCall(std::string_view cluster, const Pairs& request_headers,
                      std::string_view request_body, const Pairs& request_trailers,
                      int timeout_milliseconds, uint32_t* token_ptr) override;

  // Sends one Redis command to `cluster` (must speak RESP2) and asynchronously delivers one
  // reply via the shared queue identified by *queue_token_ptr. onQueueReady fires on the
  // *root* context, not necessarily the caller, so route by RedisCallResult::origin_context_id.
  // *call_id_ptr* identifies this call among others delivered on that queue. timeout_millis is
  // mandatory and bounded by kMaxRedisCallTimeoutMillis in context.cc.
  WasmResult redisCall(std::string_view cluster, std::vector<std::string> command_args,
                       uint32_t timeout_millis, uint32_t* queue_token_ptr, uint32_t* call_id_ptr);

  void setMaxQueuedRedisCallBytesForTesting(uint64_t max) {
    max_queued_redis_call_bytes_for_testing_ = max;
  }
  // Defined in context.cc since it needs Wasm::redisCallState(), and Wasm is only
  // forward-declared in this header.
  void setNextRedisCallIdForTesting(uint32_t next_call_id);
  void onRedisCallTimeoutForTesting(uint32_t call_id) { onRedisCallTimeout(call_id); }

  // Releases a drained redis_call result's queue slot/bytes; other shared-queue tokens fall
  // through to the base behavior.
  WasmResult dequeueSharedQueue(uint32_t token, std::string* data) override;

  // Stats/Metrics
  WasmResult defineMetric(uint32_t type, std::string_view name, uint32_t* metric_id_ptr) override;
  WasmResult incrementMetric(uint32_t metric_id, int64_t offset) override;
  WasmResult recordMetric(uint32_t metric_id, uint64_t value) override;
  WasmResult getMetric(uint32_t metric_id, uint64_t* value_ptr) override;

  // gRPC
  WasmResult grpcCall(std::string_view grpc_service, std::string_view service_name,
                      std::string_view method_name, const Pairs& initial_metadata,
                      std::string_view request, std::chrono::milliseconds timeout,
                      uint32_t* token_ptr) override;
  WasmResult grpcStream(std::string_view grpc_service, std::string_view service_name,
                        std::string_view method_name, const Pairs& initial_metadat,
                        uint32_t* token_ptr) override;

  WasmResult grpcClose(uint32_t token) override;
  WasmResult grpcCancel(uint32_t token) override;
  WasmResult grpcSend(uint32_t token, std::string_view message, bool end_stream) override;

  // Envoy specific ABI
  void onResolveDns(uint32_t token, Envoy::Network::DnsResolver::ResolutionStatus status,
                    std::list<Envoy::Network::DnsResponse>&& response);

  void onStatsUpdate(Envoy::Stats::MetricSnapshot& snapshot);

  // CEL evaluation
  std::optional<google::api::expr::runtime::CelValue>
  findValue(absl::string_view name, Protobuf::Arena* arena, bool last) const;
  std::optional<google::api::expr::runtime::CelValue>
  FindValue(absl::string_view name, Protobuf::Arena* arena) const override;

  // Foreign function state
  virtual void setForeignData(absl::string_view data_name, std::unique_ptr<StorageObject> data) {
    data_storage_[data_name] = std::move(data);
  }
  template <typename T> T* getForeignData(absl::string_view data_name) {
    const auto& it = data_storage_.find(data_name);
    if (it == data_storage_.end()) {
      return nullptr;
    }
    return dynamic_cast<T*>(it->second.get());
  }

protected:
  friend class Wasm;

  void addAfterVmCallAction(std::function<void()> f);
  void onCloseTCP();

  // Shared by redisCall() and onConfigure() so both derive the same queue name.
  static std::string redisCallQueueName(uint64_t wasm_instance_id, std::string_view root_id,
                                        std::string_view plugin_key);

  struct AsyncClientHandler : public Http::AsyncClient::Callbacks {
    // Http::AsyncClient::Callbacks
    void onSuccess(const Http::AsyncClient::Request&,
                   Envoy::Http::ResponseMessagePtr&& response) override {
      request_ = nullptr;
      context_->onHttpCallSuccess(token_, std::move(response));
    }
    void onFailure(const Http::AsyncClient::Request&,
                   Http::AsyncClient::FailureReason reason) override {
      request_ = nullptr;
      context_->onHttpCallFailure(token_, reason);
    }
    void
    onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span& /* span */,
                                 const Http::ResponseHeaderMap* /* response_headers */) override {}

    Context* context_;
    uint32_t token_;
    Http::AsyncClient::Request* request_;
  };

  struct GrpcCallClientHandler : public Grpc::RawAsyncRequestCallbacks {
    // Grpc::AsyncRequestCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap& initial_metadata) override {
      context_->onGrpcCreateInitialMetadata(token_, initial_metadata);
    }
    void onSuccessRaw(::Envoy::Buffer::InstancePtr&& response, Tracing::Span& /* span */) override {
      request_ = nullptr;
      context_->onGrpcReceiveWrapper(token_, std::move(response));
    }
    void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                   Tracing::Span& /* span */) override {
      request_ = nullptr;
      context_->onGrpcCloseWrapper(token_, status, message);
    }

    Context* context_;
    uint32_t token_;
    Grpc::RawAsyncClientSharedPtr client_;
    Grpc::AsyncRequest* request_;
  };

  struct GrpcStreamClientHandler : public Grpc::RawAsyncStreamCallbacks {
    // Grpc::AsyncStreamCallbacks
    void onCreateInitialMetadata(Http::RequestHeaderMap& initial_metadata) override {
      context_->onGrpcCreateInitialMetadata(token_, initial_metadata);
    }
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override {
      context_->onGrpcReceiveInitialMetadataWrapper(token_, std::move(metadata));
    }
    bool onReceiveMessageRaw(::Envoy::Buffer::InstancePtr&& response) override {
      context_->onGrpcReceiveWrapper(token_, std::move(response));
      return true;
    }
    void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override {
      context_->onGrpcReceiveTrailingMetadataWrapper(token_, std::move(metadata));
    }
    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
      remote_closed_ = true;
      stream_ = nullptr;
      context_->onGrpcCloseWrapper(token_, status, message);
    }

    Context* context_;
    uint32_t token_;
    Grpc::RawAsyncClientSharedPtr client_;
    Grpc::RawAsyncStream* stream_;
    bool local_closed_ = false;
    bool remote_closed_ = false;
  };

  // Speaks RESP2 over a pooled Tcp::ConnectionPool connection; one request/reply per handler.
  struct RedisCallHandler
      : public Tcp::ConnectionPool::Callbacks,
        public Tcp::ConnectionPool::UpstreamCallbacks,
        public ::Envoy::Extensions::NetworkFilters::Common::Redis::DecoderCallbacks {
    void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason, absl::string_view,
                       Upstream::HostDescriptionConstSharedPtr) override {
      cancel_handle_ = nullptr;
      context_->onRedisCallFailure(call_id_); // May destroy *this*; nothing may follow.
    }
    void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                     Upstream::HostDescriptionConstSharedPtr) override {
      cancel_handle_ = nullptr;
      conn_data_ = std::move(conn);
      conn_data_->addUpstreamCallbacks(*this);
      conn_data_->connection().write(encoded_request_, false);
    }

    // Tcp::ConnectionPool::UpstreamCallbacks / Network::ConnectionCallbacks
    void onUpstreamData(::Envoy::Buffer::Instance& data, bool) override;
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Common::Redis::DecoderCallbacks
    void
    onRespValue(::Envoy::Extensions::NetworkFilters::Common::Redis::RespValuePtr&& value) override;

    void deferredFailure();

    Context* context_; // Always rootContext(); the queue is owned by root regardless of caller.
    uint32_t call_id_;
    uint32_t origin_context_id_; // Context::id() that issued the call; see RedisCallResult.
    ::Envoy::Buffer::OwnedImpl encoded_request_;
    Tcp::ConnectionPool::Cancellable* cancel_handle_ = nullptr;
    // Null until onPoolReady; closed and nulled once the call completes, or sooner if the
    // connection becomes unsafe to reuse (see completing_ for the extra-reply case).
    Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
    ::Envoy::Extensions::NetworkFilters::Common::Redis::DecoderPtr decoder_;
    uint64_t received_bytes_ = 0; // Checked against kMaxRedisResponseBytes before each decode().
    Event::TimerPtr timer_;
    // Guards against a timer racing a just-posted completion, and against decode() reporting a
    // second, unsolicited reply on the same connection.
    bool completing_ = false;
  };

  void onHttpCallSuccess(uint32_t token, Envoy::Http::ResponseMessagePtr&& response);
  void onHttpCallFailure(uint32_t token, Http::AsyncClient::FailureReason reason);

  void onRedisCallSuccess(uint32_t call_id,
                          ::Envoy::Extensions::NetworkFilters::Common::Redis::RespValuePtr&& value);
  void onRedisCallFailure(uint32_t call_id);
  void onRedisCallTimeout(uint32_t call_id);
  // Builds and enqueues a RedisCallResult for call_id, then erases its handler. `response` is
  // only meaningful when status is Ok.
  void finishRedisCall(uint32_t call_id,
                       envoy::source::extensions::common::wasm::RedisCallResult::Status status,
                       std::string response = "");
  // Cancels and resolves every outstanding redis_call as Failure. Shared by ~Context() and
  // onDone() so a call outstanding at shutdown is resolved exactly once regardless of which
  // runs first.
  void cancelOutstandingRedisCalls();

  void onGrpcCreateInitialMetadata(uint32_t token, Http::RequestHeaderMap& metadata);
  void onGrpcReceiveInitialMetadataWrapper(uint32_t token, Http::HeaderMapPtr&& metadata);
  void onGrpcReceiveWrapper(uint32_t token, ::Envoy::Buffer::InstancePtr response);
  void onGrpcReceiveTrailingMetadataWrapper(uint32_t token, Http::HeaderMapPtr&& metadata);
  void onGrpcCloseWrapper(uint32_t token, const Grpc::Status::GrpcStatus& status,
                          const std::string_view message);

  Http::HeaderMap* getMap(WasmHeaderMapType type);
  const Http::HeaderMap* getConstMap(WasmHeaderMapType type);

  void onHeadersModified(WasmHeaderMapType type) {
    if (type != WasmHeaderMapType::RequestHeaders ||
        abi_version_ > proxy_wasm::AbiVersion::ProxyWasm_0_2_1) {
      return;
    }
    clearRouteCache();
  }

  const LocalInfo::LocalInfo* root_local_info_{nullptr}; // set only for root_context.
  PluginHandleSharedPtr plugin_handle_{nullptr};

  uint32_t next_http_call_token_ = 1;
  uint32_t next_grpc_token_ = 1; // Odd tokens are for Calls even for Streams.

  // Network callbacks.
  Network::ReadFilterCallbacks* network_read_filter_callbacks_{};
  Network::WriteFilterCallbacks* network_write_filter_callbacks_{};

  // HTTP callbacks.
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Envoy::Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};

  // Status.
  uint32_t status_code_{0};
  absl::string_view status_message_;

  // Network filter state.
  ::Envoy::Buffer::Instance* network_downstream_data_buffer_{};
  ::Envoy::Buffer::Instance* network_upstream_data_buffer_{};

  // HTTP filter state.
  Http::RequestHeaderMap* request_headers_{};
  Http::ResponseHeaderMap* response_headers_{};
  ::Envoy::Buffer::Instance* request_body_buffer_{};
  ::Envoy::Buffer::Instance* response_body_buffer_{};
  Http::RequestTrailerMap* request_trailers_{};
  Http::ResponseTrailerMap* response_trailers_{};
  Http::MetadataMap* request_metadata_{};
  Http::MetadataMap* response_metadata_{};

  // Only available during onHttpCallResponse.
  Envoy::Http::ResponseMessagePtr* http_call_response_{};

  Http::HeaderMapPtr grpc_receive_initial_metadata_;
  Http::HeaderMapPtr grpc_receive_trailing_metadata_;

  // Only available (non-nullptr) during onGrpcReceive.
  ::Envoy::Buffer::InstancePtr grpc_receive_buffer_;

  // Only available (non-nullptr) during grpcCall and grpcStream.
  Http::RequestHeaderMapPtr grpc_initial_metadata_;

  // Access log state.
  bool access_log_phase_ = false;
  const StreamInfo::StreamInfo* access_log_stream_info_{};
  const Http::RequestHeaderMap* access_log_request_headers_{};
  const Http::ResponseHeaderMap* access_log_response_headers_{};
  const Http::ResponseTrailerMap* access_log_response_trailers_{};

  // Temporary state.
  Buffer buffer_;
  bool buffering_request_body_ = false;
  bool buffering_response_body_ = false;
  bool end_of_stream_ = false;
  bool failure_local_reply_sent_ = false;

  // MB: must be a node-type map as we take persistent references to the entries.
  std::map<uint32_t, AsyncClientHandler> http_request_;
  std::map<uint32_t, GrpcCallClientHandler> grpc_call_request_;
  std::map<uint32_t, GrpcStreamClientHandler> grpc_stream_;
  // Only populated on rootContext(); keyed by call_id since every call under a root shares one
  // queue. Not persisted across RootContext replacement (unlike Wasm::RedisCallState): ~Context()
  // cancels/cleans up everything still outstanding here.
  std::map<uint32_t, RedisCallHandler> redis_call_request_;

  // Per-plugin-instance bound on completed-but-undrained redis_call result bytes sitting on the
  // shared queue (the actual counter, Wasm::RedisCallState::queued_bytes, lives on the Wasm VM so
  // it survives RootContext replacement). In-flight (not yet completed) calls are bounded only by
  // the target cluster's own circuit breakers, the same as httpCall/grpcCall.
  static constexpr uint64_t kMaxQueuedRedisCallBytes = 100 * 1024 * 1024; // 100 MiB.
  uint64_t max_queued_redis_call_bytes_for_testing_ = kMaxQueuedRedisCallBytes;

  // Set false by ~Context(); a completion posted from RedisCallHandler captures this by value to
  // detect a destroyed root before delivering.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  // Opaque state.
  absl::flat_hash_map<std::string, std::unique_ptr<StorageObject>> data_storage_;

  // TCP State.
  bool upstream_closed_ = false;
  bool downstream_closed_ = false;
  bool tcp_connection_closed_ = false;

  // Filter state prototype declaration.
  absl::flat_hash_map<std::string, Filters::Common::Expr::CelStatePrototypeConstPtr>
      state_prototypes_;

  proxy_wasm::AbiVersion abi_version_{proxy_wasm::AbiVersion::Unknown};
};
using ContextSharedPtr = std::shared_ptr<Context>;

WasmResult serializeValue(Filters::Common::Expr::CelValue value, std::string* result);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
