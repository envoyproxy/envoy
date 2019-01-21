#pragma once

#include "envoy/config/filter/http/tap/v2alpha/tap.pb.h"
#include "envoy/http/filter.h"
#include "envoy/service/tap/v2alpha/common.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "extensions/common/tap/admin.h"
#include "extensions/filters/http/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

/**
 * All stats for the tap filter. @see stats_macros.h
 */
// clang-format off
#define ALL_TAP_FILTER_STATS(COUNTER)                                                           \
  COUNTER(rq_tapped)
// clang-format on

/**
 * Wrapper struct for tap filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_TAP_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Abstract filter configuration.
 */
class FilterConfig {
public:
  virtual ~FilterConfig() = default;

  /**
   * @return the current tap configuration if there is one.
   */
  virtual HttpTapConfigSharedPtr currentConfig() PURE;

  /**
   * @return the filter stats.
   */
  virtual FilterStats& stats() PURE;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Configuration for the tap filter.
 */
class FilterConfigImpl : public FilterConfig,
                         public Extensions::Common::Tap::ExtensionConfig,
                         Logger::Loggable<Logger::Id::tap> {
public:
  FilterConfigImpl(envoy::config::filter::http::tap::v2alpha::Tap proto_config,
                   const std::string& stats_prefix, HttpTapConfigFactoryPtr&& config_factory,
                   Stats::Scope& scope, Server::Admin& admin, Singleton::Manager& singleton_manager,
                   ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_thread_dispatcher);
  ~FilterConfigImpl() override;

  // FilterConfig
  HttpTapConfigSharedPtr currentConfig() override;
  FilterStats& stats() override { return stats_; }

  // Extensions::Common::Tap::ExtensionConfig
  void clearTapConfig() override;
  const std::string& adminId() override;
  void newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Extensions::Common::Tap::Sink* admin_streamer) override;

private:
  struct TlsFilterConfig : public ThreadLocal::ThreadLocalObject {
    HttpTapConfigSharedPtr config_;
  };

  const envoy::config::filter::http::tap::v2alpha::Tap proto_config_;
  FilterStats stats_;
  HttpTapConfigFactoryPtr config_factory_;
  ThreadLocal::SlotPtr tls_slot_;
  Extensions::Common::Tap::AdminHandlerSharedPtr admin_handler_;
};

/**
 * HTTP tap filter.
 */
class Filter : public Http::StreamFilter, public AccessLog::Instance {
public:
  Filter(FilterConfigSharedPtr config)
      : config_(std::move(config)),
        tapper_(config_->currentConfig() ? config_->currentConfig()->createPerRequestTapper()
                                         : nullptr) {}

  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override {}

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

  // AccessLog::Instance
  void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
           const Http::HeaderMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info) override;

private:
  FilterConfigSharedPtr config_;
  HttpPerRequestTapperPtr tapper_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
