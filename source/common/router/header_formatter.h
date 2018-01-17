#pragma once

#include <functional>
#include <memory>
#include <sstream>
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
  virtual ~HeaderFormatter() {}

  virtual const std::string format(const Envoy::RequestInfo::RequestInfo& request_info) const PURE;

  /**
   * @return bool indicating whether the formatted header should be appended to the existing
   *              headers
   */
  virtual bool append() const PURE;
};

typedef std::unique_ptr<HeaderFormatter> HeaderFormatterPtr;

/**
 * A formatter that expands the request header variable to a value based on info in RequestInfo.
 */
class RequestInfoHeaderFormatter : public HeaderFormatter {
public:
  /*
   * Constructs a RequestInfoHeaderFormatter. The view should contain the complete variable
   * definition, include leading and trailing % symbols. It may include further content. If
   * successful, a HeaderFormatterPtr is returned and the len reference is updated with the length
   * of the variable definition, including the delimiters. Otherwise a nullptr is returned and len
   * may be the length of the variable definition (provided the trailing % was found or std::npos).
   *
   * @param var an absl::string_view containing at least the variable definition.
   * @param append controls whether the header data should be appended or not.
   * @param len a size_t& updated to contain the length of the variable definition (or std::npos
   *            if the definition is un-terminated).
   * @return a HeaderFormatterPtr on success or nullptr on failure
   */
  static HeaderFormatterPtr parse(absl::string_view var, bool append, size_t& len);

  // HeaderFormatter::format
  const std::string format(const Envoy::RequestInfo::RequestInfo& request_info) const override;
  bool append() const override { return append_; }

private:
  RequestInfoHeaderFormatter(
      std::function<std::string(const Envoy::RequestInfo::RequestInfo&)> field_extractor,
      bool append)
      : field_extractor_(field_extractor), append_(append){};

  std::function<std::string(const Envoy::RequestInfo::RequestInfo&)> field_extractor_;
  const bool append_;
};

/**
 * A formatter that returns back the same static header value.
 */
class PlainHeaderFormatter : public HeaderFormatter {
public:
  PlainHeaderFormatter(const std::string& static_header_value, bool append)
      : static_value_(static_header_value), append_(append){};

  // HeaderFormatter::format
  const std::string format(const Envoy::RequestInfo::RequestInfo&) const override {
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
      : formatters_(std::move(formatters)), append_(append){};

  // HeaderFormatter::format
  const std::string format(const Envoy::RequestInfo::RequestInfo& request_info) const override {
    std::ostringstream buf;
    for (const auto& formatter : formatters_) {
      buf << formatter->format(request_info);
    }
    return buf.str();
  };
  bool append() const override { return append_; }

private:
  const std::vector<HeaderFormatterPtr> formatters_;
  const bool append_;
};

} // namespace Router
} // namespace Envoy
