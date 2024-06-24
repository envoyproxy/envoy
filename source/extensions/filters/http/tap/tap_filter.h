#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/common/tap/extension_config_base.h"
#include "source/extensions/filters/http/tap/tap_config.h"

#include "absl/strings/string_view.h"

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
   * @return the http tap config.
   */
  virtual const envoy::extensions::filters::http::tap::v3::Tap& getTapConfig() const PURE;

  /**
   * @return the filter stats.
   */
  virtual FilterStats& stats() PURE;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Configuration for the tap filter.
 */
class FilterConfigImpl : public FilterConfig, public Extensions::Common::Tap::ExtensionConfigBase {
public:
  FilterConfigImpl(const envoy::extensions::filters::http::tap::v3::Tap& proto_config,
                   const std::string& stats_prefix,
                   Extensions::Common::Tap::TapConfigFactoryPtr&& config_factory,
                   Stats::Scope& scope, OptRef<Server::Admin> admin,
                   Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                   Event::Dispatcher& main_thread_dispatcher);

  // FilterConfig
  HttpTapConfigSharedPtr currentConfig() override;
  FilterStats& stats() override { return stats_; }
  const envoy::extensions::filters::http::tap::v3::Tap& getTapConfig() const override {
    return tap_config_;
  }

private:
  FilterStats stats_;
  const envoy::extensions::filters::http::tap::v3::Tap tap_config_;
};

/**
 * HTTP tap filter.
 */
class Filter : public Http::StreamFilter, public AccessLog::Instance {
public:
  Filter(FilterConfigSharedPtr config) : config_(std::move(config)) {}

  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    HttpTapConfigSharedPtr config = config_->currentConfig();
    if (config != nullptr) {
      auto streamId = callbacks.streamId();
      auto connection = callbacks.connection();
      tapper_ = config->createPerRequestTapper(config_->getTapConfig(), streamId, connection);
    } else {
      tapper_ = nullptr;
    }
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
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

  // AccessLog::Instance
  void log(const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo&) override;

private:
  FilterConfigSharedPtr config_;
  HttpPerRequestTapperPtr tapper_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
