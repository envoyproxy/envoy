#pragma once

#include <cctype>
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

} // namespace Http1
} // namespace Http
} // namespace Envoy
