#include "source/common/common/utility.h"

#include <algorithm>
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

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hash.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "re2/re2.h"
#include "spdlog/spdlog.h"

namespace Envoy {

namespace {

class SpecifierConstantValues {
public:
  // This pattern contains three parts:
  // 1. %[1-9*]?f: Envoy subseconds specifier with an optional width.
  // 2. %s: Envoy seconds specifier.
  // 3. %E[1-9*][Sf]: absl::FormatTime() seconds/subseconds specifier.
  const re2::RE2 RE2_PATTERN{"(%[1-9*]?f|%s|%E[1-9*][Sf])"};
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

std::string DateFormatter::fromTime(SystemTime time) const {
  // A map is used to keep different formatted format strings at a given seconds.
  static thread_local CachedTimes CACHE;
  static thread_local CachedTimes CACHE_LOCAL;

  if (specifiers_.empty()) {
    return {};
  }

  CachedTimes& cached_times = local_time_ ? CACHE_LOCAL : CACHE;

  const auto epoch_time_ss =
      std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());

  const auto iter = cached_times.find(raw_format_string_, raw_format_hash_);

  if (iter == cached_times.end() || iter->second.epoch_time_seconds != epoch_time_ss) {
    // No cached entry found for the given format string and time.

    // Remove all the expired cached items.
    for (auto it = cached_times.cbegin(); it != cached_times.cend();) {
      if (it->second.epoch_time_seconds != epoch_time_ss) {
        auto next_it = std::next(it);
        cached_times.erase(it);
        it = next_it;
      } else {
        ++it;
      }
    }

    auto new_iter = cached_times.emplace(
        std::make_pair(raw_format_string_, formatTimeAndOffsets(time, epoch_time_ss)));

    ASSERT(new_iter.second);
    return new_iter.first->second.formatted;
  }

  const auto& formatted = iter->second;
  ASSERT(specifiers_.size() == formatted.offsets.size());

  // Copy the current cached formatted format string, then replace its subseconds specifier
  // (when it has non-zero width) by correcting its position using prepared subseconds offsets.
  std::string formatted_str = formatted.formatted;

  for (size_t i = specifiers_.size(); i != 0; i--) {
    if (specifiers_[i - 1].subsecondsSpecifier()) {
      const auto offset = formatted.offsets[i - 1];
      const auto substr = specifiers_[i - 1].subsecondsToString(time);
      formatted_str.replace(offset.offset, offset.length, substr);
    }
  }
  return formatted_str;
}

void DateFormatter::parse(absl::string_view format_string) {
  absl::string_view format = format_string;
  absl::string_view matched;

  // PartialMatch will return the leftmost-longest match. And the matched will be mutated
  // to point to matched piece
  while (re2::RE2::PartialMatch(format, SpecifierConstants::get().RE2_PATTERN, &matched)) {
    ASSERT(!matched.empty());

    // Get matched position based the matched and format view. For example:
    // this-is-prefix-string-%s-this-is-suffix-string
    // |------ prefix -------||                     |
    // |                   matched                  |
    // |------------------ format ------------------|
    const size_t prefix_length = matched.data() - format.data();

    std::string prefix_string = std::string(format.substr(0, prefix_length));

    Specifier::SpecifierType specifier{};
    uint8_t width{};

    if (matched.size() == 2) {
      // Handle the seconds specifier (%s) or subseconds specifier without width (%f).

      ASSERT(matched[1] == 's' || matched[1] == 'f');
      if (matched[1] == 's') {
        specifier = Specifier::SpecifierType::Second;
      } else {
        specifier = Specifier::SpecifierType::EnvoySubsecond;
        width = 9;
      }
    } else if (matched.size() == 3) {
      // Handle subseconds specifier with width (%#f, %*f, '#' means number between 1-9,
      // '*' is literal).

      ASSERT(matched[2] == 'f');
      specifier = Specifier::SpecifierType::EnvoySubsecond;
      width = matched[1] == '*' ? std::numeric_limits<uint8_t>::max() : matched[1] - '0';
    } else {
      // To improve performance, we will cache the formatted string for every second.
      // And when doing the runtime substitution, we will get the cached result and
      // replace the subseconds part.
      // In order to make sure the subseconds part of absl::FormatTime() can be replaced,
      // we need to handle the subseconds specifier of absl::FormatTime() here.

      // Handle the absl subseconds specifier (%E#S, %E*S, %E#f, %E*f, '#' means number
      // between 1-9, '*' is literal).

      ASSERT(matched.size() == 4);
      ASSERT(matched[3] == 'S' || matched[3] == 'f');

      if (matched[3] == 'S') {
        // %E#S or %E*S includes the seconds, dot, and subseconds. For example, %E6S
        // will output "30.654321".
        // We will let the absl::FormatTime() to handle the seconds parts. So we need
        // to add the seconds specifier to the prefix string to ensure the final
        // formatted string is correct.
        prefix_string.push_back('%');
        prefix_string.push_back('S');

        specifier = Specifier::SpecifierType::AbslSubsecondS;
      } else {
        specifier = Specifier::SpecifierType::AbslSubsecondF;
      }

      width = matched[2] == '*' ? std::numeric_limits<uint8_t>::max() : matched[2] - '0';
    }

    if (!prefix_string.empty()) {
      specifiers_.push_back(Specifier(prefix_string));
    }

    ASSERT(format.size() >= prefix_length + matched.size());
    ASSERT(specifier != Specifier::SpecifierType::String);
    specifiers_.emplace_back(Specifier(specifier, width));
    format = format.substr(prefix_length + matched.size());
  }

  // To capture the segment after the last specifier pattern of a format string by creating a zero
  // width specifier. E.g. %3f-this-is-the-last-%s-segment-%Y-until-this.
  if (!format.empty()) {
    specifiers_.emplace_back(Specifier(format));
  }
}

std::string DateFormatter::Specifier::subsecondsToString(SystemTime time) const {
  ASSERT(specifier_ > SpecifierType::Second);
  ASSERT(width_ > 0);

  absl::string_view nanoseconds;

  std::string string_buffer;
  fmt::format_int formatted(std::chrono::nanoseconds(time.time_since_epoch()).count());

  // 1. Get the subseconds part of the formatted time.
  if (formatted.size() < 9) {
    // Special case. This should never happen in actual practice.
    string_buffer = std::string(9 - formatted.size(), '0');
    string_buffer.append(formatted.data(), formatted.size());
    nanoseconds = string_buffer;
  } else {
    nanoseconds = absl::string_view(formatted.data() + formatted.size() - 9, 9);
  }
  ASSERT(nanoseconds.size() == 9);

  // 2. Calculate the actual width that will be used.
  uint8_t width = width_;
  if (width == std::numeric_limits<uint8_t>::max()) {
    // Dynamic width specifier. Remove trailing zeros.
    if (const auto i = nanoseconds.find_last_not_of('0'); i != absl::string_view::npos) {
      width = i + 1;
    } else {
      // This only happens for subseconds specifiers with dynamic width (%E*S, %E*f, %*f)
      // and the subseconds are all zeros.
      width = 0;
    }
  }

  // 3. Handle the specifiers of %E#S and %E*S. The seconds part will be handled by
  // absl::FormatTime() by string specifier. So we only need to return the dot and
  // subseconds part.
  if (specifier_ == SpecifierType::AbslSubsecondS) {
    if (width == 0) {
      // No subseconds and dot for %E*S in this case.
      return {};
    }

    std::string output;
    output.push_back('.'); // Add the dot.
    output.append(nanoseconds.data(), width);
    return output;
  }

  // 4. Handle the specifiers of %E#f, %E*f, and %f, %#f, %*f. At least one subsecond digit
  // will be returned for these specifiers even if the subseconds are all zeros and dynamic
  // width is used.
  return std::string(nanoseconds.substr(0, std::max<uint8_t>(width, 1)));
}

std::string DateFormatter::Specifier::toString(SystemTime time,
                                               std::chrono::seconds epoch_time_seconds,
                                               bool local) const {
  switch (specifier_) {
  case SpecifierType::String:
    ASSERT(!string_.empty());
    // Handle the string first. It may be absl::FormatTime() format string.
    if (absl_format_) {
      return absl::FormatTime(string_, absl::FromChrono(time),
                              local ? absl::LocalTimeZone() : absl::UTCTimeZone());
    } else {
      return string_;
    }
  case SpecifierType::Second:
    // Handle the seconds specifier.
    return fmt::format_int(epoch_time_seconds.count()).str();
  case SpecifierType::EnvoySubsecond:
  case SpecifierType::AbslSubsecondS:
  case SpecifierType::AbslSubsecondF:
    // Handle the sub-seconds specifier.
    return subsecondsToString(time);
  }

  return {}; // Should never reach here. Make the gcc happy.
}

DateFormatter::CacheableTime
DateFormatter::formatTimeAndOffsets(SystemTime time,
                                    std::chrono::seconds epoch_time_seconds) const {
  CacheableTime ret;
  ret.epoch_time_seconds = epoch_time_seconds;
  ret.formatted.reserve(64);
  ret.offsets.reserve(specifiers_.size());

  for (const auto& specifier : specifiers_) {
    const auto offset = ret.formatted.size();
    ret.formatted.append(specifier.toString(time, epoch_time_seconds, local_time_));
    ret.offsets.push_back({offset, ret.formatted.size() - offset});
  }

  return ret;
}

std::string DateFormatter::now(TimeSource& time_source) const {
  return fromTime(time_source.systemTime());
}

MutableMemoryStreamBuffer::MutableMemoryStreamBuffer(char* base, size_t size) {
  this->setp(base, base + size);
}

OutputBufferStream::OutputBufferStream(char* data, size_t size)
    : MutableMemoryStreamBuffer{data, size}, std::ostream{static_cast<std::streambuf*>(this)} {}

int OutputBufferStream::bytesWritten() const { return pptr() - pbase(); }

absl::string_view OutputBufferStream::contents() const {
  const std::string::size_type written = bytesWritten();
  return {pbase(), written};
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

uint64_t DateUtil::nowToSeconds(TimeSource& time_source) {
  return std::chrono::duration_cast<std::chrono::seconds>(
             time_source.systemTime().time_since_epoch())
      .count();
}

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
                            [&](absl::string_view t) { return tokens_to_remove.contains(t); });
  return absl::StrJoin(values.begin(), end, joiner);
}

uint32_t StringUtil::itoa(char* out, size_t buffer_size, uint64_t i) {
  // The maximum size required for an unsigned 64-bit integer is 21 chars (including null).
  if (buffer_size < 21) {
    IS_ENVOY_BUG("itoa buffer too small");
    return 0;
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
  return {source.data() + start, end - start};
}

std::string StringUtil::escape(const absl::string_view source) {
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
    case '\0':
      os << "\\0";
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

std::string StringUtil::sanitizeInvalidHostname(const absl::string_view source) {
  std::string ret_str = std::string(source);
  bool sanitized = false;
  for (size_t i = 0; i < ret_str.size(); ++i) {
    if (absl::ascii_isalnum(ret_str[i]) || ret_str[i] == '.' || ret_str[i] == '-') {
      continue;
    }
    sanitized = true;
    ret_str[i] = '_';
  }

  if (sanitized) {
    ret_str = absl::StrCat("invalid:", ret_str);
  }
  return ret_str;
}

const std::string& getDefaultDateFormat(bool local_time) {
  if (local_time) {
    CONSTRUCT_ON_FIRST_USE(std::string, "%Y-%m-%dT%H:%M:%E3S%z");
  }
  CONSTRUCT_ON_FIRST_USE(std::string, "%Y-%m-%dT%H:%M:%E3SZ");
}

std::string AccessLogDateTimeFormatter::fromTime(const SystemTime& system_time, bool local_time) {
  struct CachedTime {
    std::chrono::seconds epoch_time_seconds;
    std::string formatted_time;
  };
  static thread_local CachedTime cached_time_utc;
  static thread_local CachedTime cached_time_local;

  CachedTime& cached_time = local_time ? cached_time_local : cached_time_utc;

  const std::chrono::milliseconds epoch_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch());

  const std::chrono::seconds epoch_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch_time_ms);

  if (cached_time.formatted_time.empty() || cached_time.epoch_time_seconds != epoch_time_seconds) {
    cached_time.formatted_time =
        absl::FormatTime(getDefaultDateFormat(local_time), absl::FromChrono(system_time),
                         local_time ? absl::LocalTimeZone() : absl::UTCTimeZone());
    cached_time.epoch_time_seconds = epoch_time_seconds;
  } else {
    // Overwrite the digits in the ".000Z" or ".000%z" at the end of the string with the
    // millisecond count from the input time.
    ASSERT(cached_time.formatted_time.length() >= 23);
    size_t offset = 20;
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

bool StringUtil::hasEmptySpace(absl::string_view view) {
  return view.find_first_of(WhitespaceChars) != absl::string_view::npos;
}

namespace {

using ReplacementMap = absl::flat_hash_map<std::string, std::string>;

const ReplacementMap& emptySpaceReplacement() {
  CONSTRUCT_ON_FIRST_USE(
      ReplacementMap,
      ReplacementMap{{" ", "_"}, {"\t", "_"}, {"\f", "_"}, {"\v", "_"}, {"\n", "_"}, {"\r", "_"}});
}

} // namespace

std::string StringUtil::replaceAllEmptySpace(absl::string_view view) {
  return absl::StrReplaceAll(view, emptySpaceReplacement());
}

bool Primes::isPrime(uint32_t x) {
  if (x && x < 4) {
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
  memcpy(data_, str, size); // NOLINT(safe-memcpy)
}

void ExceptionUtil::throwEnvoyException(const std::string& message) {
  throwEnvoyExceptionOrPanic(message);
}

} // namespace Envoy
