#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Formatter {

/**
 * Interface for substitution formatter.
 * Formatters provide a complete substitution output line for the given headers/trailers/stream.
 */
class Formatter {
public:
  virtual ~Formatter() = default;

  /**
   * Return a formatted substitution line.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string format(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& response_headers,
                             const Http::ResponseTrailerMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             absl::string_view local_reply_body) const PURE;
};

using FormatterPtr = std::unique_ptr<Formatter>;

/**
 * Interface for substitution provider.
 * FormatterProviders extract information from the given headers/trailers/stream.
 */
class FormatterProvider {
public:
  virtual ~FormatterProvider() = default;

  /**
   * Extract a value from the provided headers/trailers/stream.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return std::string containing a single value extracted from the given headers/trailers/stream.
   */
  virtual std::string format(const Http::RequestHeaderMap& request_headers,
                             const Http::ResponseHeaderMap& response_headers,
                             const Http::ResponseTrailerMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             absl::string_view local_reply_body) const PURE;
  /**
   * Extract a value from the provided headers/trailers/stream, preserving the value's type.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @param local_reply_body supplies the local reply body.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         headers/trailers/stream.
   */
  virtual ProtobufWkt::Value formatValue(const Http::RequestHeaderMap& request_headers,
                                         const Http::ResponseHeaderMap& response_headers,
                                         const Http::ResponseTrailerMap& response_trailers,
                                         const StreamInfo::StreamInfo& stream_info,
                                         absl::string_view local_reply_body) const PURE;
};

using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

} // namespace Formatter
} // namespace Envoy
