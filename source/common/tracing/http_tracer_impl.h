#pragma once

#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/tracing/tracer.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/common/tracing/tracer_impl.h"

namespace Envoy {
namespace Tracing {

template <class T> class HttpTraceContextBase : public TraceContext {
public:
  static_assert(std::is_same<typename std::remove_const<T>::type, Http::RequestHeaderMap>::value,
                "T must be Http::RequestHeaderMap or const Http::RequestHeaderMap");

  HttpTraceContextBase(T& request_headers) : request_headers_(request_headers) {}

  absl::string_view protocol() const override { return request_headers_.getProtocolValue(); }
  absl::string_view host() const override { return request_headers_.getHostValue(); }
  absl::string_view path() const override { return request_headers_.getPathValue(); }
  absl::string_view method() const override { return request_headers_.getMethodValue(); }
  void forEach(IterateCallback callback) const override {
    request_headers_.iterate([cb = std::move(callback)](const Http::HeaderEntry& entry) {
      if (cb(entry.key().getStringView(), entry.value().getStringView())) {
        return Http::HeaderMap::Iterate::Continue;
      }
      return Http::HeaderMap::Iterate::Break;
    });
  }
  absl::optional<absl::string_view> get(absl::string_view key) const override {
    Http::LowerCaseString lower_key{std::string(key)};
    const auto entry = request_headers_.get(lower_key);
    if (!entry.empty()) {
      return entry[0]->value().getStringView();
    }
    return absl::nullopt;
  }
  void set(absl::string_view, absl::string_view) override {}
  void remove(absl::string_view) override {}
  OptRef<const Http::RequestHeaderMap> requestHeaders() const override { return request_headers_; };
  OptRef<Http::RequestHeaderMap> requestHeaders() override { return {}; };

protected:
  T& request_headers_;
};

// Read only http trace context that could be constructed from const Http::RequestHeaderMap.
// This is mainly used for custom tag extraction.
using ReadOnlyHttpTraceContext = HttpTraceContextBase<const Http::RequestHeaderMap>;

class HttpTraceContext : public HttpTraceContextBase<Http::RequestHeaderMap> {
public:
  using HttpTraceContextBase::HttpTraceContextBase;

  void set(absl::string_view key, absl::string_view value) override {
    request_headers_.setCopy(Http::LowerCaseString(std::string(key)), value);
  }
  void remove(absl::string_view key) override {
    request_headers_.remove(Http::LowerCaseString(std::string(key)));
  }
  OptRef<const Http::RequestHeaderMap> requestHeaders() const override { return request_headers_; };
  OptRef<Http::RequestHeaderMap> requestHeaders() override { return request_headers_; };
};

class HttpTracerUtility {
public:
  /**
   * Adds information obtained from the downstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeDownstreamSpan(Span& span, const Http::RequestHeaderMap* request_headers,
                                     const Http::ResponseHeaderMap* response_headers,
                                     const Http::ResponseTrailerMap* response_trailers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config);

  /**
   * Adds information obtained from the upstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeUpstreamSpan(Span& span, const StreamInfo::StreamInfo& stream_info,
                                   const Config& tracing_config);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response headers.
   * NOOP if headers are nullptr.
   */
  static void onUpstreamResponseHeaders(Span& span,
                                        const Http::ResponseHeaderMap* response_headers);

  /**
   * Adds tags to the current "unfinished" span when processing upstream response trailers.
   * NOOP if trailers are nullptr.
   */
  static void onUpstreamResponseTrailers(Span& span,
                                         const Http::ResponseTrailerMap* response_trailers);

private:
  static void setCommonTags(Span& span, const StreamInfo::StreamInfo& stream_info,
                            const Config& tracing_config);
};

} // namespace Tracing
} // namespace Envoy
