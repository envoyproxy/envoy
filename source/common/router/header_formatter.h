#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/formatter/substitution_formatter.h"

#include "source/common/http/header_map_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * HttpHeaderFormatter is used by HTTP headers manipulators.
 **/
class HttpHeaderFormatter {
public:
  virtual ~HttpHeaderFormatter() = default;

  virtual const std::string format(const Http::RequestHeaderMap& request_headers,
                                   const Http::ResponseHeaderMap& response_headers,
                                   const Envoy::StreamInfo::StreamInfo& stream_info) const PURE;
};

using HttpHeaderFormatterPtr = std::unique_ptr<HttpHeaderFormatter>;

/**
 * Implementation of HttpHeaderFormatter.
 * Actual formatting is done via substitution formatters.
 */
class HttpHeaderFormatterImpl : public HttpHeaderFormatter {
public:
  HttpHeaderFormatterImpl(Formatter::FormatterPtr&& formatter) : formatter_(std::move(formatter)) {}

  // HttpHeaderFormatter::format
  // Trailers are not available when HTTP headers are manipulated.
  const std::string format(const Http::RequestHeaderMap& request_headers,
                           const Http::ResponseHeaderMap& response_headers,
                           const Envoy::StreamInfo::StreamInfo& stream_info) const override {
    std::string buf;
    buf = formatter_->format(request_headers, response_headers,
                             *Http::StaticEmptyHeaders::get().response_trailers, stream_info, "",
                             AccessLog::AccessLogType::NotSet);
    return buf;
  };

private:
  const Formatter::FormatterPtr formatter_;
};

} // namespace Router
} // namespace Envoy
