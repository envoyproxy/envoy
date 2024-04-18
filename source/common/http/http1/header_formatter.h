#pragma once

#include "envoy/http/header_formatter.h"

namespace Envoy {
namespace Http {
namespace Http1 {

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
