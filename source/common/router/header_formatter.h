#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/formatter/substitution_formatter.h"

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

  /**
   * @return bool indicating whether the formatted header should be appended to the existing
   *              headers or replace any existing values for the header
   */
  virtual bool append() const PURE;
};

using HeaderFormatterPtr = std::unique_ptr<HeaderFormatter>;

/**
 * A formatter that expands the request header variable to a value based on info in StreamInfo.
 */
class StreamInfoHeaderFormatter : public HeaderFormatter {
public:
  StreamInfoHeaderFormatter(absl::string_view field_name, bool append);

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const override;
  bool append() const override { return append_; }

  using FieldExtractor = std::function<std::string(const Envoy::StreamInfo::StreamInfo&)>;
  using FormatterPtrMap = absl::node_hash_map<std::string, Envoy::Formatter::FormatterPtr>;

private:
  FieldExtractor field_extractor_;
  const bool append_;

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
  PlainHeaderFormatter(const std::string& static_header_value, bool append)
      : static_value_(static_header_value), append_(append) {}

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo&) const override {
    return static_value_;
  };
  bool append() const override { return append_; }

private:
  const std::string static_value_;
  const bool append_;
};

/**
 * A formatter that produces a value by concatenating the results of multiple HeaderFormatters.
 */
class CompoundHeaderFormatter : public HeaderFormatter {
public:
  CompoundHeaderFormatter(std::vector<HeaderFormatterPtr>&& formatters, bool append)
      : formatters_(std::move(formatters)), append_(append) {}

  // HeaderFormatter::format
  const std::string format(const Envoy::StreamInfo::StreamInfo& stream_info) const override {
    std::string buf;
    for (const auto& formatter : formatters_) {
      buf += formatter->format(stream_info);
    }
    return buf;
  };
  bool append() const override { return append_; }

private:
  const std::vector<HeaderFormatterPtr> formatters_;
  const bool append_;
};

} // namespace Router
} // namespace Envoy
