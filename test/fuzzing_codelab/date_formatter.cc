#include "test/fuzzing_codelab/date_formatter.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Fuzz {

namespace {

class SpecifierConstantValues {
public:
  // This captures three groups: subsecond-specifier, subsecond-specifier width and
  // second-specifier.
  const std::regex PATTERN{"(%([1-9])?f)|(%s)", std::regex::optimize};
};

typedef ConstSingleton<SpecifierConstantValues> SpecifierConstants;

} // namespace

std::string DateFormatter::fromTime(const SystemTime& time) const {
  struct CachedTime {
    // The string length of a number of seconds since the Epoch. E.g. for "1528270093", the length
    // is 10.
    size_t seconds_length;

    // A container object to hold a strftime'd string, its timestamp (in seconds) and a list of
    // position offsets for each specifier found in a format string.
    struct Formatted {
      // The resulted string after format string is passed to strftime at a given point in time.
      std::string str;

      // A timestamp (in seconds) when this object is created.
      std::chrono::seconds epoch_time_seconds;

      // List of offsets for each specifier found in a format string. This is needed to compensate
      // the position of each recorded specifier due to the possible size change of the previous
      // segment (after strftime'd).
      SpecifierOffsets specifier_offsets;
    };
    // A map is used to keep different formatted format strings at a given second.
    std::unordered_map<std::string, const Formatted> formatted;
  };
  static thread_local CachedTime cached_time;

  const std::chrono::nanoseconds epoch_time_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch());

  const std::chrono::seconds epoch_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch_time_ns);

  const auto& item = cached_time.formatted.find(format_string_);
  if (item == cached_time.formatted.end() ||
      item->second.epoch_time_seconds != epoch_time_seconds) {
    // Remove all the expired cached items.
    for (auto it = cached_time.formatted.cbegin(); it != cached_time.formatted.cend();) {
      if (it->second.epoch_time_seconds != epoch_time_seconds) {
        it = cached_time.formatted.erase(it);
      } else {
        it++;
      }
    }

    const time_t current_time = std::chrono::system_clock::to_time_t(time);

    // Build a new formatted format string at current time.
    CachedTime::Formatted formatted;
    const std::string seconds_str = fmt::format_int(epoch_time_seconds.count()).str();
    formatted.str =
        fromTimeAndPrepareSpecifierOffsets(current_time, formatted.specifier_offsets, seconds_str);
    cached_time.seconds_length = seconds_str.size();

    // Stamp the formatted string using the current epoch time in seconds, and then cache it in.
    formatted.epoch_time_seconds = epoch_time_seconds;
    cached_time.formatted.emplace(std::make_pair(format_string_, formatted));
  }

  const auto& formatted = cached_time.formatted.at(format_string_);
  ASSERT(specifiers_.size() == formatted.specifier_offsets.size());

  // Copy the current cached formatted format string, then replace its subseconds part (when it has
  // non-zero width) by correcting its position using prepared subseconds offsets.
  std::string formatted_str = formatted.str;
  std::string nanoseconds = fmt::format_int(epoch_time_ns.count()).str();
  // Special case handling for beginning of time, we should never need to do this outside of
  // tests or a time machine.
  if (nanoseconds.size() < 10) {
    nanoseconds = std::string(10 - nanoseconds.size(), '0') + nanoseconds;
  }

  for (size_t i = 0; i < specifiers_.size(); ++i) {
    const auto& specifier = specifiers_.at(i);

    // When specifier.width_ is zero, skip the replacement. This is the last segment or it has no
    // specifier.
    if (specifier.width_ > 0 && !specifier.second_) {
      ASSERT(specifier.position_ + formatted.specifier_offsets.at(i) < formatted_str.size());
      formatted_str.replace(specifier.position_ + formatted.specifier_offsets.at(i + 1),
                            specifier.width_,
                            nanoseconds.substr(cached_time.seconds_length, specifier.width_));
    }
  }

  ASSERT(formatted_str.size() == formatted.str.size());
  return formatted_str;
}

std::string DateFormatter::parse(const std::string& format_string) {
  std::string new_format_string = format_string;
  std::smatch matched;
  size_t step = 0;
  while (regex_search(new_format_string, matched, SpecifierConstants::get().PATTERN)) {
    // The std::smatch matched for (%([1-9])?f)|(%s): [all, subsecond-specifier, subsecond-specifier
    // width, second-specifier].
    const std::string& width_specifier = matched[2];
    const std::string& second_specifier = matched[3];

    // In the template string to be used in runtime substitution, the width is the number of
    // characters to be replaced.
    const size_t width = width_specifier.empty() ? 9 : width_specifier.at(0) - '0';
    new_format_string.replace(matched.position(), matched.length(),
                              std::string(second_specifier.empty() ? width : 2, '?'));

    ASSERT(step < new_format_string.size());

    // This records matched position, the width of current subsecond pattern, and also the string
    // segment before the matched position. These values will be used later at data path.
    specifiers_.emplace_back(
        second_specifier.empty()
            ? Specifier(matched.position(), width,
                        new_format_string.substr(step, matched.position() - step))
            : Specifier(matched.position(),
                        new_format_string.substr(step, matched.position() - step)));

    step = specifiers_.back().position_ + specifiers_.back().width_;
  }

  // To capture the segment after the last specifier pattern of a format string by creating a zero
  // width specifier. E.g. %3f-this-is-the-last-%s-segment-%Y-until-this.
  if (step < new_format_string.size()) {
    Specifier specifier(step, 0, new_format_string.substr(step));
    specifiers_.emplace_back(specifier);
  }

  return new_format_string;
}

std::string DateFormatter::fromTime(time_t time) const {
  tm current_tm;
  gmtime_r(&time, &current_tm);

  std::vector<char> buf;
  const size_t len = strftime(&buf[0], buf.size(), format_string_.c_str(), &current_tm);
  return std::string(&buf[0], len);
}

std::string
DateFormatter::fromTimeAndPrepareSpecifierOffsets(time_t time, SpecifierOffsets& specifier_offsets,
                                                  const std::string& seconds_str) const {
  tm current_tm;
  gmtime_r(&time, &current_tm);

  std::array<char, 1024> buf;
  std::string formatted;

  size_t previous = 0;
  specifier_offsets.reserve(specifiers_.size());
  for (const auto& specifier : specifiers_) {
    const size_t formatted_length =
        strftime(&buf[0], buf.size(), specifier.segment_.c_str(), &current_tm);
    absl::StrAppend(&formatted, absl::string_view(&buf[0], formatted_length),
                    specifier.second_ ? seconds_str : std::string(specifier.width_, '?'));

    // This computes and saves offset of each specifier's pattern to correct its position after the
    // previous string segment is formatted. An offset can be a negative value.
    //
    // If the current specifier is a second specifier (%s), it needs to be corrected by 2.
    const int32_t offset = (formatted_length + (specifier.second_ ? (seconds_str.size() - 2) : 0)) -
                           specifier.segment_.size();
    specifier_offsets.emplace_back(previous + offset);
    previous += offset;
  }

  return formatted;
}

std::string DateFormatter::now() {
  time_t current_time_t;
  time(&current_time_t);
  return fromTime(current_time_t);
}

} // namespace Fuzz
} // namespace Envoy
