#pragma once

#include <string>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @brief Helper for creating and reading the Dynatrace tag in the tracestate http header
 * This tag has at least 8 values delimited by semicolon:
 * - tag[0]: version (currently version 4)
 * - tag[1] - tag[4]: unused in the sampler (always 0)
 * - tag[5]: ignored field. 1 if a span is ignored (not sampled), 0 otherwise
 * - tag[6]: sampling exponent
 * - tag[7]: path info
 */
class DynatraceTag {
public:
  static DynatraceTag createInvalid() { return {false, false, 0, 0}; }

  // Creates a tag using the given values.
  static DynatraceTag create(bool ignored, uint32_t sampling_exponent, uint32_t path_info) {
    return {true, ignored, sampling_exponent, path_info};
  }

  // Creates a DynatraceTag from the value in the tracestate
  static DynatraceTag create(const std::string& value) {
    std::vector<absl::string_view> tracestate_components =
        absl::StrSplit(value, ';', absl::AllowEmpty());
    if (tracestate_components.size() < 8) {
      return createInvalid();
    }

    if (tracestate_components[0] != "fw4") {
      return createInvalid();
    }
    bool ignored = tracestate_components[5] == "1";
    uint32_t sampling_exponent;
    uint32_t path_info;
    if (absl::SimpleAtoi(tracestate_components[6], &sampling_exponent) &&
        absl::SimpleHexAtoi(tracestate_components[7], &path_info)) {
      return {true, ignored, sampling_exponent, path_info};
    }
    return createInvalid();
  }

  // Returns a DynatraceTag as string.
  std::string asString() const {
    std::string ret = absl::StrCat("fw4;0;0;0;0;", ignored_ ? "1" : "0", ";", sampling_exponent_,
                                   ";", absl::Hex(path_info_));
    return ret;
  }

  // Returns true if parsing was successful.
  bool isValid() const { return valid_; };

  // Returns true if the ignored flag is set.
  bool isIgnored() const { return ignored_; };

  // Returns the sampling exponent.
  uint32_t getSamplingExponent() const { return sampling_exponent_; };

private:
  DynatraceTag(bool valid, bool ignored, uint32_t sampling_exponent, uint32_t path_info)
      : valid_(valid), ignored_(ignored), sampling_exponent_(sampling_exponent),
        path_info_(path_info) {}

  const bool valid_;
  const bool ignored_;
  const uint32_t sampling_exponent_;
  const uint32_t path_info_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
