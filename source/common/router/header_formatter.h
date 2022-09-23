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
 * Interface for all types of header formatters used for custom request headers.
 */
class HeaderFormatter {
public:
  virtual ~HeaderFormatter() = default;

  virtual const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const PURE;
};

using HeaderFormatterPtr = std::unique_ptr<HeaderFormatter>;

/**
 * A formatter that expands the request header variable to a value based on info in StreamInfo.
 */
class StreamInfoHeaderFormatter : public HeaderFormatter {
public:
  StreamInfoHeaderFormatter(absl::string_view field_name);

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const override;

  using FieldExtractor = std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>;
  using FormatterPtrMap = absl::node_hash_map<std::string, Envoy::Formatter::FormatterPtr>;

private:
  FieldExtractor field_extractor_;

  // Maps a string format pattern (including field name and any command operators between
  // parenthesis) to the list of FormatterProviderPtrs that are capable of formatting that pattern.
  // We use a map here to make sure that we only create a single parser for a given format pattern
  // even if it appears multiple times in the larger formatting context (e.g. it shows up multiple
  // times in a format string).
  FormatterPtrMap formatter_map_;
};

/**
 * A formatter that returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {
public:
  PlainHeaderFormatter(const std::string& static_header_value)
      : static_value_(static_header_value) {}

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo&) const override {
    return static_value_;
  };

private:
  const std::string static_value_;
};

/**
 * A formatter that produces a value by concatenating the results of multiple HeaderFormatters.
 */
class CompoundHeaderFormatter : public HeaderFormatter {
public:
  CompoundHeaderFormatter(std::vector<HeaderFormatterPtr>&& formatters)
      : formatters_(std::move(formatters)) {}

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const override {
    std::string buf;
    for (const auto& formatter : formatters_) {
      buf += formatter->format(stream_info);
    }
    return buf;
  };

private:
  const std::vector<HeaderFormatterPtr> formatters_;
};

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
                             *Http::StaticEmptyHeaders::get().response_trailers, stream_info, "");
    return buf;
  };

private:
  const Formatter::FormatterPtr formatter_;
};

// TODO(cpakulski).
// This class is used only when runtime guard envoy_reloadable_features_unified_header_formatter
// is false and should be removed when the guard is removed. See issue 20389.
// This is basically a bridge between "new" header formatters which take request_headers and
// response_headers as parameters and "old" header formatters which take only stream_info as
// parameter.
class HttpHeaderFormatterBridge : public HttpHeaderFormatter {
public:
  HttpHeaderFormatterBridge() = delete;
  HttpHeaderFormatterBridge(HeaderFormatterPtr&& header_formatter)
      : header_formatter_(std::move(header_formatter)) {}

  const std::string format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                           const Envoy::StreamInfo::StreamInfo& stream_info) const override {
    return header_formatter_->format(stream_info);
  }

private:
  const HeaderFormatterPtr header_formatter_;
};

} // namespace Router
} // namespace Envoy
