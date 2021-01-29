#include "common/common/utility.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ios>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/singleton/const_singleton.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "spdlog/spdlog.h"

namespace Envoy {

namespace {

class SpecifierConstantValues {
public:
  // This captures three groups: subsecond-specifier, subsecond-specifier width and
  // second-specifier.
  const std::regex PATTERN{"(%([1-9])?f)|(%s)", std::regex::optimize};
};

using SpecifierConstants = ConstSingleton<SpecifierConstantValues>;
using UnsignedMilliseconds = std::chrono::duration<uint64_t, std::milli>;

} // namespace

const std::string errorDetails(int error_code) {
#ifndef WIN32
  // clang-format off
  return strerror(error_code);
  // clang-format on
#else
  // Windows error codes do not correspond to POSIX errno values
  // Use FormatMessage, strip trailing newline, and return "Unknown error" on failure (as on POSIX).
  // Failures will usually be due to the error message not being found.
  char* buffer = NULL;
  DWORD msg_size = FormatMessage(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
      NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&buffer, 0, NULL);
  if (msg_size == 0) {
    return "Unknown error";
  }
  if (msg_size > 1 && buffer[msg_size - 2] == '\r' && buffer[msg_size - 1] == '\n') {
    msg_size -= 2;
  }
  std::string error_details(buffer, msg_size);
  ASSERT(LocalFree(buffer) == NULL);
  return error_details;
#endif
}

std::string DateFormatter::fromTime(const SystemTime& time) const {
  struct CachedTime {
    // The string length of a number of seconds since the Epoch. E.g. for "1528270093", the length
    // is 10.
    size_t seconds_length;

    // A container object to hold a absl::FormatTime string, its timestamp (in seconds) and a list
    // of position offsets for each specifier found in a format string.
    struct Formatted {
      // The resulted string after format string is passed to absl::FormatTime at a given point in
      // time.
      std::string str;

      // A timestamp (in seconds) when this object is created.
      std::chrono::seconds epoch_time_seconds;

      // List of offsets for each specifier found in a format string. This is needed to compensate
      // the position of each recorded specifier due to the possible size change of the previous
      // segment (after being formatted).
      SpecifierOffsets specifier_offsets;
    };
    // A map is used to keep different formatted format strings at a given second.
    absl::node_hash_map<std::string, const Formatted> formatted;
  };
  static thread_local CachedTime cached_time;

  const std::chrono::nanoseconds epoch_time_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch());

  const std::chrono::seconds epoch_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch_time_ns);

  const auto& item = cached_time.formatted.find(raw_format_string_);
  if (item == cached_time.formatted.end() ||
      item->second.epoch_time_seconds != epoch_time_seconds) {
    // Remove all the expired cached items.
    for (auto it = cached_time.formatted.cbegin(); it != cached_time.formatted.cend();) {
      if (it->second.epoch_time_seconds != epoch_time_seconds) {
        auto next_it = std::next(it);
        cached_time.formatted.erase(it);
        it = next_it;
      } else {
        ++it;
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
    cached_time.formatted.emplace(std::make_pair(raw_format_string_, formatted));
  }

  const auto& formatted = cached_time.formatted.at(raw_format_string_);
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
      formatted_str.replace(specifier.position_ + formatted.specifier_offsets.at(i),
                            specifier.width_,
                            nanoseconds.substr(cached_time.seconds_length, specifier.width_));
    }
  }

  ASSERT(formatted_str.size() == formatted.str.size());
  return formatted_str;
}

void DateFormatter::parse(const std::string& format_string) {
  std::string suffix = format_string;
  std::smatch matched;
  // "step" is the last specifier's position + the last specifier's width. It's not the current
  // position in "format_string" because the length has changed. It is actually the index which
  // points to the end of the last specifier in formatted string (generated in the future).
  size_t step = 0;
  while (regex_search(suffix, matched, SpecifierConstants::get().PATTERN)) {
    // The std::smatch matched for (%([1-9])?f)|(%s): [all, subsecond-specifier, subsecond-specifier
    // width, second-specifier].
    const std::string& width_specifier = matched[2];
    const std::string& second_specifier = matched[3];

    // In the template string to be used in runtime substitution, the width is the number of
    // characters to be replaced.
    const size_t width = width_specifier.empty() ? 9 : width_specifier.at(0) - '0';

    ASSERT(!suffix.empty());
    // This records matched position, the width of current subsecond pattern, and also the string
    // segment before the matched position. These values will be used later at data path.
    specifiers_.emplace_back(
        second_specifier.empty()
            ? Specifier(step + matched.position(), width, suffix.substr(0, matched.position()))
            : Specifier(step + matched.position(), suffix.substr(0, matched.position())));
    step = specifiers_.back().position_ + specifiers_.back().width_;
    suffix = matched.suffix();
  }

  // To capture the segment after the last specifier pattern of a format string by creating a zero
  // width specifier. E.g. %3f-this-is-the-last-%s-segment-%Y-until-this.
  if (!suffix.empty()) {
    Specifier specifier(step, 0, suffix);
    specifiers_.emplace_back(specifier);
  }
}

std::string
DateFormatter::fromTimeAndPrepareSpecifierOffsets(time_t time, SpecifierOffsets& specifier_offsets,
                                                  const std::string& seconds_str) const {
  std::string formatted_time;

  int32_t previous = 0;
  specifier_offsets.reserve(specifiers_.size());
  for (const auto& specifier : specifiers_) {
    std::string current_format =
        absl::FormatTime(specifier.segment_, absl::FromTimeT(time), absl::UTCTimeZone());
    absl::StrAppend(&formatted_time, current_format,
                    specifier.second_ ? seconds_str : std::string(specifier.width_, '?'));

    // This computes and saves offset of each specifier's pattern to correct its position after the
    // previous string segment is formatted. An offset can be a negative value.
    //
    // If the current specifier is a second specifier (%s), it needs to be corrected by 2.
    const int32_t offset =
        (current_format.length() + (specifier.second_ ? (seconds_str.size() - 2) : 0)) -
        specifier.segment_.size();
    specifier_offsets.emplace_back(previous + offset);
    previous += offset;
  }

  return formatted_time;
}

std::string DateFormatter::now(TimeSource& time_source) {
  return fromTime(time_source.systemTime());
}

MutableMemoryStreamBuffer::MutableMemoryStreamBuffer(char* base, size_t size) {
  this->setp(base, base + size);
}

OutputBufferStream::OutputBufferStream(char* data, size_t size)
    : MutableMemoryStreamBuffer{data, size}, std::ostream{static_cast<std::streambuf*>(this)} {}

int OutputBufferStream::bytesWritten() const { return pptr() - pbase(); }

absl::string_view OutputBufferStream::contents() const {
  return absl::string_view(pbase(), bytesWritten());
}

ConstMemoryStreamBuffer::ConstMemoryStreamBuffer(const char* data, size_t size) {
  // std::streambuf won't modify `data`, but the interface still requires a char* for convenience,
  // so we need to const_cast.
  char* ptr = const_cast<char*>(data);

  this->setg(ptr, ptr, ptr + size);
}

InputConstMemoryStream::InputConstMemoryStream(const char* data, size_t size)
    : ConstMemoryStreamBuffer{data, size}, std::istream{static_cast<std::streambuf*>(this)} {}

bool DateUtil::timePointValid(SystemTime time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
             .count() != 0;
}

bool DateUtil::timePointValid(MonotonicTime time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
             .count() != 0;
}

uint64_t DateUtil::nowToMilliseconds(TimeSource& time_source) {
  const SystemTime& now = time_source.systemTime();
  return std::chrono::time_point_cast<UnsignedMilliseconds>(now).time_since_epoch().count();
}

const char StringUtil::WhitespaceChars[] = " \t\f\v\n\r";

const char* StringUtil::strtoull(const char* str, uint64_t& out, int base) {
  if (strlen(str) == 0) {
    return nullptr;
  }

  char* end_ptr;
  errno = 0;
  out = std::strtoull(str, &end_ptr, base);
  if (end_ptr == str || (out == ULLONG_MAX && errno == ERANGE)) {
    return nullptr;
  } else {
    return end_ptr;
  }
}

bool StringUtil::atoull(const char* str, uint64_t& out, int base) {
  const char* end_ptr = StringUtil::strtoull(str, out, base);
  if (end_ptr == nullptr || *end_ptr != '\0') {
    return false;
  } else {
    return true;
  }
}

absl::string_view StringUtil::ltrim(absl::string_view source) {
  const absl::string_view::size_type pos = source.find_first_not_of(WhitespaceChars);
  if (pos != absl::string_view::npos) {
    source.remove_prefix(pos);
  } else {
    source.remove_prefix(source.size());
  }
  return source;
}

absl::string_view StringUtil::rtrim(absl::string_view source) {
  const absl::string_view::size_type pos = source.find_last_not_of(WhitespaceChars);
  if (pos != absl::string_view::npos) {
    source.remove_suffix(source.size() - pos - 1);
  } else {
    source.remove_suffix(source.size());
  }
  return source;
}

absl::string_view StringUtil::trim(absl::string_view source) { return ltrim(rtrim(source)); }

absl::string_view StringUtil::removeTrailingCharacters(absl::string_view source, char ch) {
  const absl::string_view::size_type pos = source.find_last_not_of(ch);
  if (pos != absl::string_view::npos) {
    source.remove_suffix(source.size() - pos - 1);
  } else {
    source.remove_suffix(source.size());
  }
  return source;
}

bool StringUtil::findToken(absl::string_view source, absl::string_view delimiters,
                           absl::string_view key_token, bool trim_whitespace) {
  const auto tokens = splitToken(source, delimiters, trim_whitespace);
  if (trim_whitespace) {
    for (const auto& token : tokens) {
      if (key_token == trim(token)) {
        return true;
      }
    }
    return false;
  }

  return std::find(tokens.begin(), tokens.end(), key_token) != tokens.end();
}

bool StringUtil::caseFindToken(absl::string_view source, absl::string_view delimiters,
                               absl::string_view key_token, bool trim_whitespace) {
  const auto tokens = splitToken(source, delimiters, trim_whitespace);
  std::function<bool(absl::string_view)> predicate;

  if (trim_whitespace) {
    predicate = [&](absl::string_view token) {
      return absl::EqualsIgnoreCase(key_token, trim(token));
    };
  } else {
    predicate = [&](absl::string_view token) { return absl::EqualsIgnoreCase(key_token, token); };
  }

  return std::find_if(tokens.begin(), tokens.end(), predicate) != tokens.end();
}

absl::string_view StringUtil::cropRight(absl::string_view source, absl::string_view delimiter) {
  const absl::string_view::size_type pos = source.find(delimiter);
  if (pos != absl::string_view::npos) {
    source.remove_suffix(source.size() - pos);
  }
  return source;
}

absl::string_view StringUtil::cropLeft(absl::string_view source, absl::string_view delimiter) {
  const absl::string_view::size_type pos = source.find(delimiter);
  if (pos != absl::string_view::npos) {
    source.remove_prefix(pos + delimiter.size());
  }
  return source;
}

std::vector<absl::string_view> StringUtil::splitToken(absl::string_view source,
                                                      absl::string_view delimiters,
                                                      bool keep_empty_string,
                                                      bool trim_whitespace) {
  std::vector<absl::string_view> result;
  if (keep_empty_string) {
    result = absl::StrSplit(source, absl::ByAnyChar(delimiters));
  } else {
    if (trim_whitespace) {
      result = absl::StrSplit(source, absl::ByAnyChar(delimiters), absl::SkipWhitespace());
    } else {
      result = absl::StrSplit(source, absl::ByAnyChar(delimiters), absl::SkipEmpty());
    }
  }

  if (trim_whitespace) {
    for_each(result.begin(), result.end(), [](auto& v) { v = trim(v); });
  }
  return result;
}

std::string StringUtil::removeTokens(absl::string_view source, absl::string_view delimiters,
                                     const CaseUnorderedSet& tokens_to_remove,
                                     absl::string_view joiner) {
  auto values = Envoy::StringUtil::splitToken(source, delimiters, false, true);
  auto end = std::remove_if(values.begin(), values.end(),
                            [&](absl::string_view t) { return tokens_to_remove.count(t) != 0; });
  return absl::StrJoin(values.begin(), end, joiner);
}

uint32_t StringUtil::itoa(char* out, size_t buffer_size, uint64_t i) {
  // The maximum size required for an unsigned 64-bit integer is 21 chars (including null).
  if (buffer_size < 21) {
    throw std::invalid_argument("itoa buffer too small");
  }

  char* current = out;
  do {
    *current++ = "0123456789"[i % 10];
    i /= 10;
  } while (i > 0);

  for (uint64_t i = 0, j = current - out - 1; i < j; i++, j--) {
    char c = out[i];
    out[i] = out[j];
    out[j] = c;
  }

  *current = 0;
  return static_cast<uint32_t>(current - out);
}

size_t StringUtil::strlcpy(char* dst, const char* src, size_t size) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
  return strlen(src);
}

std::string StringUtil::subspan(absl::string_view source, size_t start, size_t end) {
  return std::string(source.data() + start, end - start);
}

std::string StringUtil::escape(const std::string& source) {
  std::string ret;

  // Prevent unnecessary allocation by allocating 2x original size.
  ret.reserve(source.length() * 2);
  for (char c : source) {
    switch (c) {
    case '\r':
      ret += "\\r";
      break;
    case '\n':
      ret += "\\n";
      break;
    case '\t':
      ret += "\\t";
      break;
    case '"':
      ret += "\\\"";
      break;
    default:
      ret += c;
      break;
    }
  }

  return ret;
}

// TODO(kbaichoo): If needed, add support for escaping chars < 32 and >= 127.
void StringUtil::escapeToOstream(std::ostream& os, absl::string_view view) {
  for (const char c : view) {
    switch (c) {
    case '\r':
      os << "\\r";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\t':
      os << "\\t";
      break;
    case '\v':
      os << "\\v";
      break;
    case '"':
      os << "\\\"";
      break;
    case '\'':
      os << "\\\'";
      break;
    case '\\':
      os << "\\\\";
      break;
    default:
      os << c;
      break;
    }
  }
}

const std::string& getDefaultDateFormat() {
  CONSTRUCT_ON_FIRST_USE(std::string, "%Y-%m-%dT%H:%M:%E3SZ");
}

std::string AccessLogDateTimeFormatter::fromTime(const SystemTime& system_time) {
  struct CachedTime {
    std::chrono::seconds epoch_time_seconds;
    std::string formatted_time;
  };
  static thread_local CachedTime cached_time;

  const std::chrono::milliseconds epoch_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch());

  const std::chrono::seconds epoch_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch_time_ms);

  if (cached_time.formatted_time.empty() || cached_time.epoch_time_seconds != epoch_time_seconds) {
    cached_time.formatted_time = absl::FormatTime(
        getDefaultDateFormat(), absl::FromChrono(system_time), absl::UTCTimeZone());
    cached_time.epoch_time_seconds = epoch_time_seconds;
  } else {
    // Overwrite the digits in the ".000Z" at the end of the string with the
    // millisecond count from the input time.
    ASSERT(cached_time.formatted_time.length() == 24);
    size_t offset = cached_time.formatted_time.length() - 4;
    uint32_t msec = epoch_time_ms.count() % 1000;
    cached_time.formatted_time[offset++] = ('0' + (msec / 100));
    msec %= 100;
    cached_time.formatted_time[offset++] = ('0' + (msec / 10));
    msec %= 10;
    cached_time.formatted_time[offset++] = ('0' + msec);
  }

  return cached_time.formatted_time;
}

const std::string& StringUtil::nonEmptyStringOrDefault(const std::string& s,
                                                       const std::string& default_value) {
  return s.empty() ? default_value : s;
}

std::string StringUtil::toUpper(absl::string_view s) {
  std::string upper_s;
  upper_s.reserve(s.size());
  std::transform(s.cbegin(), s.cend(), std::back_inserter(upper_s), absl::ascii_toupper);
  return upper_s;
}

bool StringUtil::CaseInsensitiveCompare::operator()(absl::string_view lhs,
                                                    absl::string_view rhs) const {
  return absl::EqualsIgnoreCase(lhs, rhs);
}

uint64_t StringUtil::CaseInsensitiveHash::operator()(absl::string_view key) const {
  return HashUtil::djb2CaseInsensitiveHash(key);
}

std::string StringUtil::removeCharacters(const absl::string_view& str,
                                         const IntervalSet<size_t>& remove_characters) {
  std::string ret;
  size_t pos = 0;
  const auto intervals = remove_characters.toVector();
  std::vector<absl::string_view> pieces;
  pieces.reserve(intervals.size());
  for (const auto& [left_bound, right_bound] : intervals) {
    if (left_bound != pos) {
      ASSERT(right_bound <= str.size());
      pieces.push_back(str.substr(pos, left_bound - pos));
    }
    pos = right_bound;
  }
  if (pos != str.size()) {
    pieces.push_back(str.substr(pos));
  }
  return absl::StrJoin(pieces, "");
}

bool Primes::isPrime(uint32_t x) {
  if (x < 4) {
    return true; // eliminates special-casing 2.
  } else if ((x & 1) == 0) {
    return false; // eliminates even numbers >2.
  }

  uint32_t limit = sqrt(x);
  for (uint32_t factor = 3; factor <= limit; factor += 2) {
    if ((x % factor) == 0) {
      return false;
    }
  }
  return true;
}

uint32_t Primes::findPrimeLargerThan(uint32_t x) {
  x += (x % 2) + 1;
  while (!isPrime(x)) {
    x += 2;
  }
  return x;
}

// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm
void WelfordStandardDeviation::update(double new_value) {
  ++count_;
  const double delta = new_value - mean_;
  mean_ += delta / count_;
  const double delta2 = new_value - mean_;
  m2_ += delta * delta2;
}

double WelfordStandardDeviation::computeVariance() const {
  if (count_ < 2) {
    return std::nan("");
  }
  return m2_ / (count_ - 1);
}

double WelfordStandardDeviation::computeStandardDeviation() const {
  const double variance = computeVariance();
  // It seems very difficult for variance to go negative, but from the calculation in update()
  // above, I can't quite convince myself it's impossible, so put in a guard to be sure.
  return (std::isnan(variance) || variance < 0) ? std::nan("") : sqrt(variance);
}

InlineString::InlineString(const char* str, size_t size) : size_(size) {
  RELEASE_ASSERT(size <= 0xffffffff, "size must fit in 32 bits");
  memcpy(data_, str, size);
}

void ExceptionUtil::throwEnvoyException(const std::string& message) {
  throw EnvoyException(message);
}

} // namespace Envoy
