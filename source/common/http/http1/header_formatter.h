#pragma once

#include <cctype>
#include <map>
#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class HeaderKeyFormatter {
public:
  virtual ~HeaderKeyFormatter() = default;

  virtual std::string format(absl::string_view key) const PURE;
};

using HeaderKeyFormatterPtr = std::unique_ptr<HeaderKeyFormatter>;

/**
 * A HeaderKeyFormatter that upper cases the first character in each word: The
 * first character as well as any alpha character following a special
 * character is upper cased.
 */
class ProperCaseHeaderKeyFormatter : public HeaderKeyFormatter {
public:
  std::string format(absl::string_view key) const override;
};

/**
 * A HeaderKeyFormatter that supports custom rules.
 */
class CustomHeaderKeyFormatter : public HeaderKeyFormatter {
public:
  CustomHeaderKeyFormatter(const std::map<std::string, std::string> &rules) : rules_(rules) {}
  std::string format(absl::string_view key) const override;
private:
  const std::map<std::string, std::string> &rules_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
