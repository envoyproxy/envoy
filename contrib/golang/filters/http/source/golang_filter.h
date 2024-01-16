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
#include "contrib/golang/common/dso/dso.h"
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

  GolangFilterStats stats_;

  Dso::HttpFilterDsoPtr dso_lib_;
  uint64_t config_id_{0};
  // TODO(StarryVae): use rwlock.
  Thread::MutexBasicLockable mutex_{};
  MetricStoreSharedPtr metric_store_ ABSL_GUARDED_BY(mutex_);
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
  httpConfig* config_{nullptr};
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

struct httpRequestInternal;

/**
 * See docs/configuration/http_filters/golang_extension_filter.rst
 */
class Filter : public Http::StreamFilter,
               public std::enable_shared_from_this<Filter>,
               public Filters::Common::Expr::StreamActivation,
               Logger::Loggable<Logger::Id::http>,
               public AccessLog::Instance {
public:
  explicit Filter(FilterConfigSharedPtr config, Dso::HttpFilterDsoPtr dynamic_lib)
      : config_(config), dynamic_lib_(dynamic_lib), decoding_state_(*this), encoding_state_(*this) {
  }

  // Http::StreamFilterBase
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

  void onStreamComplete() override {}

  CAPIStatus continueStatus(GolangStatus status);

  CAPIStatus sendLocalReply(Http::Code response_code, std::string body_text,
                            std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                            Grpc::Status::GrpcStatus grpc_status, std::string details);

  CAPIStatus sendPanicReply(absl::string_view details);

  CAPIStatus getHeader(absl::string_view key, uint64_t* value_data, int* value_len);
  CAPIStatus copyHeaders(GoString* go_strs, char* go_buf);
  CAPIStatus setHeader(absl::string_view key, absl::string_view value, headerAction act);
  CAPIStatus removeHeader(absl::string_view key);
  CAPIStatus copyBuffer(Buffer::Instance* buffer, char* data);
  CAPIStatus drainBuffer(Buffer::Instance* buffer, uint64_t length);
  CAPIStatus setBufferHelper(Buffer::Instance* buffer, absl::string_view& value,
                             bufferAction action);
  CAPIStatus copyTrailers(GoString* go_strs, char* go_buf);
  CAPIStatus setTrailer(absl::string_view key, absl::string_view value, headerAction act);
  CAPIStatus removeTrailer(absl::string_view key);
  CAPIStatus getStringValue(int id, uint64_t* value_data, int* value_len);
  CAPIStatus getIntegerValue(int id, uint64_t* value);

  CAPIStatus getDynamicMetadata(const std::string& filter_name, uint64_t* buf_data, int* buf_len);
  CAPIStatus setDynamicMetadata(std::string filter_name, std::string key, absl::string_view buf);
  CAPIStatus setStringFilterState(absl::string_view key, absl::string_view value, int state_type,
                                  int life_span, int stream_sharing);
  CAPIStatus getStringFilterState(absl::string_view key, uint64_t* value_data, int* value_len);
  CAPIStatus getStringProperty(absl::string_view path, uint64_t* value_data, int* value_len,
                               GoInt32* rc);

private:
  bool hasDestroyed() {
    Thread::LockGuard lock(mutex_);
    return has_destroyed_;
  };
  ProcessorState& getProcessorState();

  bool doHeaders(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers, bool end_stream);
  GolangStatus doHeadersGo(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers,
                           bool end_stream);
  bool doData(ProcessorState& state, Buffer::Instance&, bool);
  bool doDataGo(ProcessorState& state, Buffer::Instance& data, bool end_stream);
  bool doTrailer(ProcessorState& state, Http::HeaderMap& trailers);
  bool doTrailerGo(ProcessorState& state, Http::HeaderMap& trailers);

  void initRequest(ProcessorState& state);

  uint64_t getMergedConfigId(ProcessorState& state);

  void continueEncodeLocalReply(ProcessorState& state);
  void continueStatusInternal(GolangStatus status);
  void continueData(ProcessorState& state);

  void onHeadersModified();

  void sendLocalReplyInternal(Http::Code response_code, absl::string_view body_text,
                              std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                              Grpc::Status::GrpcStatus grpc_status, absl::string_view details);

  void setDynamicMetadataInternal(ProcessorState& state, std::string filter_name, std::string key,
                                  const absl::string_view& buf);

  void populateSliceWithMetadata(ProcessorState& state, const std::string& filter_name,
                                 uint64_t* buf_data, int* buf_len);

  CAPIStatus getStringPropertyCommon(absl::string_view path, uint64_t* value_data, int* value_len,
                                     ProcessorState& state);
  CAPIStatus getStringPropertyInternal(absl::string_view path, std::string* result);
  absl::optional<google::api::expr::runtime::CelValue> findValue(absl::string_view name,
                                                                 Protobuf::Arena* arena);
  CAPIStatus serializeStringValue(Filters::Common::Expr::CelValue value, std::string* result);

  const FilterConfigSharedPtr config_;
  Dso::HttpFilterDsoPtr dynamic_lib_;

  Http::RequestOrResponseHeaderMap* headers_ ABSL_GUARDED_BY(mutex_){nullptr};
  Http::HeaderMap* trailers_ ABSL_GUARDED_BY(mutex_){nullptr};

  // save temp values from local reply
  Http::RequestOrResponseHeaderMap* local_headers_{nullptr};
  Http::HeaderMap* local_trailers_{nullptr};

  // save temp values for fetching request attributes in the later phase,
  // like getting request size
  Http::RequestOrResponseHeaderMap* request_headers_{nullptr};

  // The state of the filter on both the encoding and decoding side.
  DecodingProcessorState decoding_state_;
  EncodingProcessorState encoding_state_;

  httpRequestInternal* req_{nullptr};

  // lock for has_destroyed_ and the functions get/set/copy/remove/etc that operate on the
  // headers_/trailers_/etc, to avoid race between envoy c thread and go thread (when calling back
  // from go). it should also be okay without this lock in most cases, just for extreme case.
  Thread::MutexBasicLockable mutex_{};
  bool has_destroyed_ ABSL_GUARDED_BY(mutex_){false};

  // other filter trigger sendLocalReply during go processing in async.
  // will wait go return before continue.
  // this variable is read/write in safe thread, do no need lock.
  bool local_reply_waiting_go_{false};

  // the filter enter encoding phase
  bool enter_encoding_{false};
};

// Go code only touch the fields in httpRequest
struct httpRequestInternal : httpRequest {
  std::weak_ptr<Filter> filter_;
  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string strValue;
  httpRequestInternal(std::weak_ptr<Filter> f) { filter_ = f; }
  std::weak_ptr<Filter> weakFilter() { return filter_; }
};

struct httpConfigInternal : httpConfig {
  std::weak_ptr<FilterConfig> config_;
  httpConfigInternal(std::weak_ptr<FilterConfig> c) { config_ = c; }
  std::weak_ptr<FilterConfig> weakFilterConfig() { return config_; }
};

class GoStringFilterState : public StreamInfo::FilterState::Object {
public:
  GoStringFilterState(absl::string_view value) : value_(value) {}
  const std::string& value() const { return value_; }

private:
  const std::string value_;
};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
