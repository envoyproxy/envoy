#pragma once

#include <atomic>

#include "envoy/access_log/access_log.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/thread.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "contrib/envoy/extensions/filters/http/golang/v3alpha/golang.pb.h"
#include "contrib/golang/filters/http/source/processor_state.h"
#include "contrib/golang/filters/http/source/stats.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

enum class MetricType {
  Counter = 0,
  Gauge = 1,
  Histogram = 2,
  Max = 2,
};

class MetricStore {
public:
  MetricStore(Stats::ScopeSharedPtr scope) : scope_(scope) {}

  static constexpr uint32_t kMetricTypeMask = 0x3;
  static constexpr uint32_t kMetricIdIncrement = 0x4;

  uint32_t nextCounterMetricId() { return next_counter_metric_id_ += kMetricIdIncrement; }
  uint32_t nextGaugeMetricId() { return next_gauge_metric_id_ += kMetricIdIncrement; }
  uint32_t nextHistogramMetricId() { return next_histogram_metric_id_ += kMetricIdIncrement; }

  absl::flat_hash_map<uint32_t, Stats::Counter*> counters_;
  absl::flat_hash_map<uint32_t, Stats::Gauge*> gauges_;
  absl::flat_hash_map<uint32_t, Stats::Histogram*> histograms_;

  Stats::ScopeSharedPtr scope_;

private:
  uint32_t next_counter_metric_id_ = static_cast<uint32_t>(MetricType::Counter);
  uint32_t next_gauge_metric_id_ = static_cast<uint32_t>(MetricType::Gauge);
  uint32_t next_histogram_metric_id_ = static_cast<uint32_t>(MetricType::Histogram);
};

using MetricStoreSharedPtr = std::shared_ptr<MetricStore>;

struct httpConfigInternal;

/**
 * Configuration for the HTTP golang extension filter.
 */
class FilterConfig : public std::enable_shared_from_this<FilterConfig>,
                     Logger::Loggable<Logger::Id::http> {
public:
  FilterConfig(const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
               Dso::HttpFilterDsoPtr dso_lib, const std::string& stats_prefix,
               Server::Configuration::FactoryContext& context);
  ~FilterConfig();

  const std::string& soId() const { return so_id_; }
  const std::string& soPath() const { return so_path_; }
  const std::string& pluginName() const { return plugin_name_; }
  uint64_t getConfigId();
  GolangFilterStats& stats() { return stats_; }

  void newGoPluginConfig();
  CAPIStatus defineMetric(uint32_t metric_type, absl::string_view name, uint32_t* metric_id);
  CAPIStatus incrementMetric(uint32_t metric_id, int64_t offset);
  CAPIStatus getMetric(uint32_t metric_id, uint64_t* value);
  CAPIStatus recordMetric(uint32_t metric_id, uint64_t value);

private:
  const std::string plugin_name_;
  const std::string so_id_;
  const std::string so_path_;
  const ProtobufWkt::Any plugin_config_;
  uint32_t concurrency_;

  GolangFilterStats stats_;

  Dso::HttpFilterDsoPtr dso_lib_;
  uint64_t config_id_{0};
  // TODO(StarryVae): use rwlock.
  Thread::MutexBasicLockable mutex_{};
  MetricStoreSharedPtr metric_store_ ABSL_GUARDED_BY(mutex_);
  // filter level config is created in C++ side, and freed by Golang GC finalizer.
  httpConfigInternal* config_{nullptr};
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class RoutePluginConfig : public std::enable_shared_from_this<RoutePluginConfig>,
                          Logger::Loggable<Logger::Id::http> {
public:
  RoutePluginConfig(const std::string plugin_name,
                    const envoy::extensions::filters::http::golang::v3alpha::RouterPlugin& config);
  ~RoutePluginConfig();
  uint64_t getConfigId();
  uint64_t getMergedConfigId(uint64_t parent_id);

private:
  const std::string plugin_name_;
  const ProtobufWkt::Any plugin_config_;

  Dso::HttpFilterDsoPtr dso_lib_;
  uint64_t config_id_{0};
  // since these two fields are updated in worker threads, we need to protect them with a mutex.
  uint64_t merged_config_id_ ABSL_GUARDED_BY(mutex_){0};
  uint64_t cached_parent_id_ ABSL_GUARDED_BY(mutex_){0};

  absl::Mutex mutex_;
  // route level config, no Golang GC finalizer.
  httpConfig config_;
};

using RoutePluginConfigPtr = std::shared_ptr<RoutePluginConfig>;

/**
 * Route configuration for the filter.
 */
class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig,
                             Logger::Loggable<Logger::Id::http> {
public:
  FilterConfigPerRoute(const envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute&,
                       Server::Configuration::ServerFactoryContext&);
  uint64_t getPluginConfigId(uint64_t parent_id, std::string plugin_name) const;

  ~FilterConfigPerRoute() override { plugins_config_.clear(); }

private:
  std::map<std::string, RoutePluginConfigPtr> plugins_config_;
};

enum class DestroyReason {
  Normal,
  Terminate,
};

enum class EnvoyValue {
  RouteName = 1,
  FilterChainName,
  Protocol,
  ResponseCode,
  ResponseCodeDetails,
  AttemptCount,
  DownstreamLocalAddress,
  DownstreamRemoteAddress,
  UpstreamLocalAddress,
  UpstreamRemoteAddress,
  UpstreamClusterName,
  VirtualClusterName,
};

class Filter;

// Go code only touch the fields in httpRequest
class HttpRequestInternal : public httpRequest {
public:
  HttpRequestInternal(Filter& filter)
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

// Wrapper HttpRequestInternal to DeferredDeletable.
// Since we want keep httpRequest at the top of the HttpRequestInternal,
// so, HttpRequestInternal can not inherit the virtual class DeferredDeletable.
class HttpRequestInternalWrapper : public Envoy::Event::DeferredDeletable {
public:
  HttpRequestInternalWrapper(HttpRequestInternal* req) : req_(req) {}
  ~HttpRequestInternalWrapper() override { delete req_; }

private:
  HttpRequestInternal* req_;
};

/**
 * See docs/configuration/http_filters/golang_extension_filter.rst
 */
class Filter : public Http::StreamFilter,
               public std::enable_shared_from_this<Filter>,
               public Filters::Common::Expr::StreamActivation,
               Logger::Loggable<Logger::Id::http>,
               public AccessLog::Instance {
public:
  explicit Filter(FilterConfigSharedPtr config, Dso::HttpFilterDsoPtr dynamic_lib,
                  uint32_t worker_id)
      : config_(config), dynamic_lib_(dynamic_lib), req_(new HttpRequestInternal(*this)),
        decoding_state_(req_->decodingState()), encoding_state_(req_->encodingState()) {
    // req is used by go, so need to use raw memory and then it is safe to release at the gc
    // finalize phase of the go object.
    req_->plugin_name.data = config_->pluginName().data();
    req_->plugin_name.len = config_->pluginName().length();
    req_->worker_id = worker_id;
    ENVOY_LOG(debug, "initilizing Golang Filter, decode state: {}, encode state: {}",
              decoding_state_.stateStr(), encoding_state_.stateStr());
  }

  // Http::StreamFilterBase
  void onStreamComplete() override;
  void onDestroy() ABSL_LOCKS_EXCLUDED(mutex_) override;
  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoding_state_.setDecoderFilterCallbacks(callbacks);
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoding_state_.setEncoderFilterCallbacks(callbacks);
  }

  // AccessLog::Instance
  void log(const Formatter::HttpFormatterContext& log_context,
           const StreamInfo::StreamInfo& info) override;

  CAPIStatus clearRouteCache();
  CAPIStatus continueStatus(ProcessorState& state, GolangStatus status);

  CAPIStatus sendLocalReply(ProcessorState& state, Http::Code response_code, std::string body_text,
                            std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                            Grpc::Status::GrpcStatus grpc_status, std::string details);

  CAPIStatus sendPanicReply(ProcessorState& state, absl::string_view details);

  CAPIStatus getHeader(ProcessorState& state, absl::string_view key, uint64_t* value_data,
                       int* value_len);
  CAPIStatus copyHeaders(ProcessorState& state, GoString* go_strs, char* go_buf);
  CAPIStatus setHeader(ProcessorState& state, absl::string_view key, absl::string_view value,
                       headerAction act);
  CAPIStatus removeHeader(ProcessorState& state, absl::string_view key);
  CAPIStatus copyBuffer(ProcessorState& state, Buffer::Instance* buffer, char* data);
  CAPIStatus drainBuffer(ProcessorState& state, Buffer::Instance* buffer, uint64_t length);
  CAPIStatus setBufferHelper(ProcessorState& state, Buffer::Instance* buffer,
                             absl::string_view& value, bufferAction action);
  CAPIStatus copyTrailers(ProcessorState& state, GoString* go_strs, char* go_buf);
  CAPIStatus setTrailer(ProcessorState& state, absl::string_view key, absl::string_view value,
                        headerAction act);
  CAPIStatus removeTrailer(ProcessorState& state, absl::string_view key);

  CAPIStatus getStringValue(int id, uint64_t* value_data, int* value_len);
  CAPIStatus getIntegerValue(int id, uint64_t* value);

  CAPIStatus getDynamicMetadata(const std::string& filter_name, uint64_t* buf_data, int* buf_len);
  CAPIStatus setDynamicMetadata(std::string filter_name, std::string key, absl::string_view buf);
  CAPIStatus setStringFilterState(absl::string_view key, absl::string_view value, int state_type,
                                  int life_span, int stream_sharing);
  CAPIStatus getStringFilterState(absl::string_view key, uint64_t* value_data, int* value_len);
  CAPIStatus getStringProperty(absl::string_view path, uint64_t* value_data, int* value_len,
                               GoInt32* rc);

  bool isProcessingInGo() {
    return decoding_state_.isProcessingInGo() || encoding_state_.isProcessingInGo();
  }
  void deferredDeleteRequest(HttpRequestInternal* req);

private:
  bool hasDestroyed() {
    Thread::LockGuard lock(mutex_);
    return has_destroyed_;
  };
  const StreamInfo::StreamInfo& streamInfo() const { return decoding_state_.streamInfo(); }
  StreamInfo::StreamInfo& streamInfo() { return decoding_state_.streamInfo(); }
  bool isThreadSafe() { return decoding_state_.isThreadSafe(); };
  Event::Dispatcher& getDispatcher() { return decoding_state_.getDispatcher(); }

  bool doHeaders(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers, bool end_stream);
  GolangStatus doHeadersGo(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers,
                           bool end_stream);
  bool doData(ProcessorState& state, Buffer::Instance&, bool);
  bool doDataGo(ProcessorState& state, Buffer::Instance& data, bool end_stream);
  bool doTrailer(ProcessorState& state, Http::HeaderMap& trailers);
  bool doTrailerGo(ProcessorState& state, Http::HeaderMap& trailers);

  // return true when it is first inited.
  bool initRequest();

  uint64_t getMergedConfigId();

  void continueEncodeLocalReply(ProcessorState& state);
  void continueStatusInternal(ProcessorState& state, GolangStatus status);
  void continueData(ProcessorState& state);

  void sendLocalReplyInternal(ProcessorState& state, Http::Code response_code,
                              absl::string_view body_text,
                              std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                              Grpc::Status::GrpcStatus grpc_status, absl::string_view details);

  void setDynamicMetadataInternal(std::string filter_name, std::string key,
                                  const absl::string_view& buf);

  void populateSliceWithMetadata(const std::string& filter_name, uint64_t* buf_data, int* buf_len);

  CAPIStatus getStringPropertyCommon(absl::string_view path, uint64_t* value_data, int* value_len);
  CAPIStatus getStringPropertyInternal(absl::string_view path, std::string* result);
  absl::optional<google::api::expr::runtime::CelValue> findValue(absl::string_view name,
                                                                 Protobuf::Arena* arena);
  CAPIStatus serializeStringValue(Filters::Common::Expr::CelValue value, std::string* result);

  const FilterConfigSharedPtr config_;
  Dso::HttpFilterDsoPtr dynamic_lib_;

  // save temp values for fetching request attributes in the later phase,
  // like getting request size
  Http::RequestHeaderMap* request_headers_{nullptr};
  Http::RequestTrailerMap* request_trailers_{nullptr};

  HttpRequestInternal* req_{nullptr};

  // The state of the filter on both the encoding and decoding side.
  // They are stored in HttpRequestInternal since Go need to read them,
  // And it's safe to read them before onDestroy in C++ side.
  DecodingProcessorState& decoding_state_;
  EncodingProcessorState& encoding_state_;

  // lock for has_destroyed_/etc, to avoid race between envoy c thread and go thread (when calling
  // back from go).
  Thread::MutexBasicLockable mutex_{};
  bool has_destroyed_ ABSL_GUARDED_BY(mutex_){false};
};

struct httpConfigInternal : httpConfig {
  std::weak_ptr<FilterConfig> config_;
  httpConfigInternal(std::weak_ptr<FilterConfig> c) { config_ = c; }
  std::weak_ptr<FilterConfig> weakFilterConfig() { return config_; }
};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
