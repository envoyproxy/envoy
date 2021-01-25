#pragma once

#include "envoy/http/filter.h"

#include "common/common/logger.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

class PlatformBridgeFilter;

class PlatformBridgeFilterConfig {
public:
  PlatformBridgeFilterConfig(
      const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& proto_config);

  const std::string& filter_name() { return filter_name_; }
  const envoy_http_filter* platform_filter() const { return platform_filter_; }

private:
  const std::string filter_name_;
  const envoy_http_filter* platform_filter_;
};

typedef std::shared_ptr<PlatformBridgeFilterConfig> PlatformBridgeFilterConfigSharedPtr;

enum class IterationState { Ongoing, Stopped };

/**
 * Harness to bridge Envoy filter invocations up to the platform layer.
 *
 * This filter enables filter implementations to be written in high-level platform-specific
 * languages and run within the Envoy filter chain. To mirror platform API conventions, the
 * semantic structure of platform filters differs slightly from Envoy filters. Platform
 * filter invocations (on-headers, on-data, etc.) receive *immutable* entities as parameters
 * and are expected to return compound results that include both the filter status, as well
 * as any desired modifications to the HTTP entity. Additionally, when platform filters
 * stop iteration, they _must_ use a new ResumeIteration status to resume iteration
 * at a later point. The Continue status is only valid if iteration is already ongoing.
 *
 * For more information on implementing platform filters, see the docs.
 */
class PlatformBridgeFilter final : public Http::PassThroughFilter,
                                   public Logger::Loggable<Logger::Id::filter>,
                                   public std::enable_shared_from_this<PlatformBridgeFilter> {
public:
  PlatformBridgeFilter(PlatformBridgeFilterConfigSharedPtr config, Event::Dispatcher& dispatcher);

  // Asynchronously trigger resumption of filter iteration, if applicable.
  // This is a no-op if filter iteration is already ongoing.
  void resumeDecoding();

  // Asynchronously trigger resumption of filter iteration, if applicable.
  // This is a no-op if filter iteration is already ongoing.
  void resumeEncoding();

  // StreamFilterBase
  void onDestroy() override;

  // StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // StreamEncoderFilter
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  /**
   * Internal delegate for managing logic and state that exists for both the request (decoding)
   * and response (encoding) paths.
   */
  struct FilterBase : public Logger::Loggable<Logger::Id::filter> {
    FilterBase(PlatformBridgeFilter& parent, envoy_filter_on_headers_f on_headers,
               envoy_filter_on_data_f on_data, envoy_filter_on_trailers_f on_trailers,
               envoy_filter_on_resume_f on_resume)
        : iteration_state_(IterationState::Ongoing), parent_(parent), on_headers_(on_headers),
          on_data_(on_data), on_trailers_(on_trailers), on_resume_(on_resume) {}

    static FilterBase createRequestBase(PlatformBridgeFilter& parent);

    static FilterBase createResponseBase(PlatformBridgeFilter& parent);

    Http::FilterHeadersStatus onHeaders(Http::HeaderMap& headers, bool end_stream);

    Http::FilterDataStatus onData(Buffer::Instance& data, bool end_stream,
                                  Buffer::Instance* internal_buffer);

    Http::FilterTrailersStatus onTrailers(Http::HeaderMap& trailers,
                                          Buffer::Instance* internal_buffer);

    // Scheduled on the dispatcher when resume* is called from platform
    // filter callbacks. Provides a snapshot of pending HTTP stream state to the
    // platform filter, and consumes invocation results to modify pending HTTP
    // entities before resuming iteration.
    void onResume(Buffer::Instance* internal_buffer, std::function<void()> resume_call);

    IterationState iteration_state_;
    PlatformBridgeFilter& parent_;
    envoy_filter_on_headers_f on_headers_;
    envoy_filter_on_data_f on_data_;
    envoy_filter_on_trailers_f on_trailers_;
    envoy_filter_on_resume_f on_resume_;
    bool stream_complete_{};
    Http::HeaderMap* pending_headers_{};
    Http::HeaderMap* pending_trailers_{};
  };

  Event::Dispatcher& dispatcher_;
  const std::string filter_name_;
  envoy_http_filter platform_filter_;
  FilterBase request_filter_base_;
  FilterBase response_filter_base_;
  envoy_http_filter_callbacks platform_request_callbacks_{};
  envoy_http_filter_callbacks platform_response_callbacks_{};
  bool error_response_{};
};

using PlatformBridgeFilterSharedPtr = std::shared_ptr<PlatformBridgeFilter>;
using PlatformBridgeFilterWeakPtr = std::weak_ptr<PlatformBridgeFilter>;

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
