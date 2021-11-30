#pragma once

#include "envoy/common/scope_tracker.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

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

using PlatformBridgeFilterConfigSharedPtr = std::shared_ptr<PlatformBridgeFilterConfig>;

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
                                   public ScopeTrackedObject,
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

  // Asynchronously reset the stream idle timeout. Does not affect other timeouts.
  void resetIdleTimer();

  // StreamFilterBase
  void onDestroy() override;
  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override;

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

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level = 0) const override;

  // Common stream instrumentation.
  envoy_final_stream_intel finalStreamIntel();
  envoy_stream_intel streamIntel();

  // Filter state.
  bool isAlive() { return alive_; }

private:
  /**
   * Internal delegate for managing logic and state that exists for both the request (decoding)
   * and response (encoding) paths.
   */
  struct FilterBase : public Logger::Loggable<Logger::Id::filter> {
    FilterBase(PlatformBridgeFilter& parent, envoy_filter_on_headers_f on_headers,
               envoy_filter_on_data_f on_data, envoy_filter_on_trailers_f on_trailers,
               envoy_filter_on_resume_f on_resume)
        : parent_(parent), on_headers_(on_headers), on_data_(on_data), on_trailers_(on_trailers),
          on_resume_(on_resume) {
      state_.iteration_state_ = IterationState::Ongoing;
    }

    virtual ~FilterBase() = default;

    // Common handling for both request and response path.
    Http::FilterHeadersStatus onHeaders(Http::HeaderMap& headers, bool end_stream);
    Http::FilterDataStatus onData(Buffer::Instance& data, bool end_stream);
    Http::FilterTrailersStatus onTrailers(Http::HeaderMap& trailers);

    // Scheduled on the dispatcher when resumeDecoding/Encoding is called from platform
    // filter callbacks. Provides a snapshot of pending HTTP stream state to the
    // platform filter, and consumes invocation results to modify pending HTTP
    // entities before resuming iteration.
    void onResume();

    // Common stream instrumentation.
    envoy_stream_intel streamIntel() { return parent_.streamIntel(); }

    // Debugging instrumentation.
    void dumpState(std::ostream& os, int indent_level = 0);

    // Directional (request/response) helper methods.
    virtual void addData(envoy_data data) PURE;
    virtual void addTrailers(envoy_headers trailers) PURE;
    virtual void resumeIteration() PURE;
    virtual Buffer::Instance* buffer() PURE;

    // Struct that collects Filter state for keeping track of state transitions and to report via
    // dumpState.
    struct FilterState {
      IterationState iteration_state_;
      bool on_headers_called_{};
      bool headers_forwarded_{};
      bool on_data_called_{};
      bool data_forwarded_{};
      bool on_trailers_called_{};
      bool trailers_forwarded_{};
      bool on_resume_called_{};
      bool stream_complete_{};
    };

    PlatformBridgeFilter& parent_;
    FilterState state_;
    envoy_filter_on_headers_f on_headers_;
    envoy_filter_on_data_f on_data_;
    envoy_filter_on_trailers_f on_trailers_;
    envoy_filter_on_resume_f on_resume_;
    Http::HeaderMap* pending_headers_{};
    Http::HeaderMap* pending_trailers_{};
  };

  struct RequestFilterBase : FilterBase {
    RequestFilterBase(PlatformBridgeFilter& parent)
        : FilterBase(parent, parent.platform_filter_.on_request_headers,
                     parent.platform_filter_.on_request_data,
                     parent.platform_filter_.on_request_trailers,
                     parent.platform_filter_.on_resume_request) {}

    // FilterBase
    void addData(envoy_data data) override;
    void addTrailers(envoy_headers trailers) override;
    void resumeIteration() override;
    Buffer::Instance* buffer() override;
  };

  using RequestFilterBasePtr = std::unique_ptr<RequestFilterBase>;

  struct ResponseFilterBase : FilterBase {
    ResponseFilterBase(PlatformBridgeFilter& parent)
        : FilterBase(parent, parent.platform_filter_.on_response_headers,
                     parent.platform_filter_.on_response_data,
                     parent.platform_filter_.on_response_trailers,
                     parent.platform_filter_.on_resume_response) {}

    // FilterBase
    void addData(envoy_data data) override;
    void addTrailers(envoy_headers trailers) override;
    void resumeIteration() override;
    Buffer::Instance* buffer() override;
  };

  using ResponseFilterBasePtr = std::unique_ptr<ResponseFilterBase>;

  Event::ScopeTracker& scopeTracker() const { return dispatcher_; }

  Event::Dispatcher& dispatcher_;
  const std::string filter_name_;
  envoy_http_filter platform_filter_;
  RequestFilterBasePtr request_filter_base_{};
  ResponseFilterBasePtr response_filter_base_{};
  envoy_http_filter_callbacks platform_request_callbacks_{};
  envoy_http_filter_callbacks platform_response_callbacks_{};
  bool error_response_{};
  bool alive_{true};
};

using PlatformBridgeFilterSharedPtr = std::shared_ptr<PlatformBridgeFilter>;
using PlatformBridgeFilterWeakPtr = std::weak_ptr<PlatformBridgeFilter>;

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
