#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Interface for all types of header formatters used for custom request headers.
 */
class HeaderFormatter {
public:
  HeaderFormatter(bool append, bool skip_if_present)
      : append_(append), skip_if_present_(skip_if_present) {}
  virtual ~HeaderFormatter() = default;

  virtual const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * @return bool indicating whether the formatted header should be appended to the existing
   *              headers
   */
  bool append() const { return append_; }

  /**
   * @return bool indicating whether the formatted header should be skipped if the existing header
   * is already set.
   */
  bool skipIfPresent() const { return skip_if_present_; }

private:
  const bool append_;
  const bool skip_if_present_;
};

using HeaderFormatterPtr = std::unique_ptr<HeaderFormatter>;

/**
 * A formatter that expands the request header variable to a value based on info in StreamInfo.
 */
class StreamInfoHeaderFormatter : public HeaderFormatter {
public:
  StreamInfoHeaderFormatter(absl::string_view field_name, bool append, bool skip_if_present);

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const override;

  using FieldExtractor = std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>;

private:
  FieldExtractor field_extractor_;
  std::unordered_map<std::string, std::vector<Envoy::AccessLog::FormatterProviderPtr>>
      start_time_formatters_;
};

/**
 * A formatter that returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {
public:
  PlainHeaderFormatter(const std::string& static_header_value, bool append, bool skip_if_present)
      : HeaderFormatter(append, skip_if_present), static_value_(static_header_value) {}

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
  CompoundHeaderFormatter(std::vector<HeaderFormatterPtr>&& formatters, bool append,
                          bool skip_if_present)
      : HeaderFormatter(append, skip_if_present), formatters_(std::move(formatters)) {}

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

} // namespace Router
} // namespace Envoy
