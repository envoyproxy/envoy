#pragma once

#include <chrono>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

#include "envoy/common/time.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

/**
 * Utility class for formatting dates given a strftime style format string.
 */
class DateFormatter {
public:
  DateFormatter(const std::string& format_string) : format_string_(parse(format_string)) {}

  /**
   * @return std::string representing the GMT/UTC time based on the input time.
   */
  std::string fromTime(const SystemTime& time) const;

  /**
   * @return std::string representing the GMT/UTC time based on the input time.
   */
  std::string fromTime(time_t time) const;

  /**
   * @return std::string representing the current GMT/UTC time based on the format string.
   */
  std::string now();

  /**
   * @return std::string the format string used.
   */
  const std::string& formatString() const { return format_string_; }

private:
  std::string parse(const std::string& format_string);

  typedef std::vector<int32_t> SpecifierOffsets;
  std::string fromTimeAndPrepareSpecifierOffsets(time_t time, SpecifierOffsets& specifier_offsets,
                                                 const std::string& seconds_str) const;

  // A container to hold a specifiers (%f, %Nf, %s) found in a format string.
  struct Specifier {
    // To build a subsecond-specifier.
    Specifier(const size_t position, const size_t width, const std::string& segment)
        : position_(position), width_(width), segment_(segment), second_(false) {}

    // To build a second-specifier (%s), the number of characters to be replaced is always 2.
    Specifier(const size_t position, const std::string& segment)
        : position_(position), width_(2), segment_(segment), second_(true) {}

    // The position/index of a specifier in a format string.
    const size_t position_;

    // The width of a specifier, e.g. given %3f, the width is 3. If %f is set as the
    // specifier, the width value should be 9 (the number of nanosecond digits).
    const size_t width_;

    // The string before the current specifier's position and after the previous found specifier. A
    // segment may include strftime accepted specifiers. E.g. given "%3f-this-i%s-a-segment-%4f",
    // the current specifier is "%4f" and the segment is "-this-i%s-a-segment-".
    const std::string segment_;

    // As an indication that this specifier is a %s (expect to be replaced by seconds since the
    // epoch).
    const bool second_;
  };

  // This holds all specifiers found in a given format string.
  std::vector<Specifier> specifiers_;

  const std::string format_string_;
};

} // namespace Fuzz
} // namespace Envoy
