#pragma once

#include <strings.h>

#include <chrono>
#include <cstdint>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/common/interval_set.h"
#include "envoy/common/time.h"

#include "common/common/assert.h"
#include "common/common/hash.h"

#include "absl/strings/string_view.h"

namespace Envoy {
/**
 * Utility class for formatting dates given a strftime style format string.
 */
class DateFormatter {
public:
  DateFormatter(const std::string& format_string) : format_string_(format_string) {}

  /**
   * @return std::string representing the GMT/UTC time based on the input time.
   */
  std::string fromTime(const SystemTime& time);

  /**
   * @return std::string representing the current GMT/UTC time based on the format string.
   */
  std::string now();

private:
  std::string fromTimeT(time_t time);

  std::string format_string_;
};

/**
 * Utility class for access log date/time format with milliseconds support.
 */
class AccessLogDateTimeFormatter {
public:
  static std::string fromTime(const SystemTime& time);
};

/**
 * Production implementation of SystemTimeSource that returns the current time.
 */
class ProdSystemTimeSource : public SystemTimeSource {
public:
  // SystemTimeSource
  SystemTime currentTime() override { return std::chrono::system_clock::now(); }

  static ProdSystemTimeSource instance_;
};

/**
 * Production implementation of MonotonicTimeSource that returns the current time.
 */
class ProdMonotonicTimeSource : public MonotonicTimeSource {
public:
  // MonotonicTimeSource
  MonotonicTime currentTime() override { return std::chrono::steady_clock::now(); }

  static ProdMonotonicTimeSource instance_;
};

/**
 * Class used for creating non-copying std::istream's. See InputConstMemoryStream below.
 */
class ConstMemoryStreamBuffer : public std::streambuf {
public:
  ConstMemoryStreamBuffer(const char* data, size_t size);
};

/**
 * std::istream class similar to std::istringstream, except that it provides a view into a region of
 * constant memory. It can be more efficient than std::istringstream because it doesn't copy the
 * provided string.
 *
 * See https://stackoverflow.com/a/13059195/4447365.
 */
class InputConstMemoryStream : public virtual ConstMemoryStreamBuffer, public std::istream {
public:
  InputConstMemoryStream(const char* data, size_t size);
};

/**
 * Utility class for date/time helpers.
 */
class DateUtil {
public:
  /**
   * @return whether a time_point contains a valid, not default constructed time.
   */
  static bool timePointValid(SystemTime time_point);

  /**
   * @return whether a time_point contains a valid, not default constructed time.
   */
  static bool timePointValid(MonotonicTime time_point);
};

/**
 * Utility routines for working with strings.
 */
class StringUtil {
public:
  static const char WhitespaceChars[];

  /**
   * Convert a string to an unsigned long, checking for error.
   * @param return TRUE if successful, FALSE otherwise.
   */
  static bool atoul(const char* str, uint64_t& out, int base = 10);

  /**
   * Perform a case insensitive compare of 2 strings.
   * @param lhs supplies string 1.
   * @param rhs supplies string 2.
   * @return < 0, 0, > 0 depending on the comparison result.
   */
  static int caseInsensitiveCompare(const char* lhs, const char* rhs) {
    return strcasecmp(lhs, rhs);
  }

  /**
   * Convert an unsigned integer to a base 10 string as fast as possible.
   * @param out supplies the string to fill.
   * @param out_len supplies the length of the output buffer. Must be >= MIN_ITOA_OUT_LEN.
   * @param i supplies the number to convert.
   * @return the size of the string, not including the null termination.
   */
  static constexpr size_t MIN_ITOA_OUT_LEN = 21;
  static uint32_t itoa(char* out, size_t out_len, uint64_t i);

  /**
   * Trim leading whitespace from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view ltrim(absl::string_view source);

  /**
   * Trim trailing whitespaces from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view rtrim(absl::string_view source);

  /**
   * Trim leading and trailing whitespaces from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view trim(absl::string_view source);

  /**
   * Look up for an exactly token in a delimiter-separated string view.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param token supplies the lookup string view.
   * @param trim_whitespace remove leading and trailing whitespaces from each of the split
   * string views; default = true.
   * @return true if found and false otherwise.
   *
   * E.g.,
   *
   * findToken("A=5; b", "=;", "5")   . true
   * findToken("A=5; b", "=;", "A=5") . false
   * findToken("A=5; b", "=;", "A")   . true
   * findToken("A=5; b", "=;", "b")   . true
   * findToken("A=5", ".", "A=5")     . true
   */
  static bool findToken(absl::string_view source, absl::string_view delimiters,
                        absl::string_view token, bool trim_whitespace = true);

  /**
   * Look up for a token in a delimiter-separated string view ignoring case
   * sensitivity.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param token supplies the lookup string view.
   * @param trim_whitespace remove leading and trailing whitespaces from each of the split
   * string views; default = true.
   * @return true if found a string that is semantically the same and false otherwise.
   *
   * E.g.,
   *
   * findToken("hello; world", ";", "HELLO")   . true
   */
  static bool caseFindToken(absl::string_view source, absl::string_view delimiters,
                            absl::string_view key_token, bool trim_whitespace = true);

  /**
   * Compare one string view with another string view ignoring case sensitivity.
   * @param lhs supplies the first string view.
   * @param rhs supplies the second string view.
   * @return true if strings are semantically the same and false otherwise.
   *
   * E.g.,
   *
   * findToken("hello; world", ";", "HELLO")   . true
   */
  static bool caseCompare(absl::string_view lhs, absl::string_view rhs);

  /**
   * Crop characters from a string view starting at the first character of the matched
   * delimiter string view until the end of the source string view.
   * @param source supplies the string view to be processed.
   * @param delimiter supplies the string view that delimits the starting point for deletion.
   * @return sub-string of the string view if any.
   *
   * E.g.,
   *
   * cropRight("foo ; ; ; ; ; ; ", ";") == "foo "
   */
  static absl::string_view cropRight(absl::string_view source, absl::string_view delimiters);

  /**
   * Crop characters from a string view starting at the first character of the matched
   * delimiter string view until the begining of the source string view.
   * @param source supplies the string view to be processed.
   * @param delimiter supplies the string view that delimits the starting point for deletion.
   * @return sub-string of the string view if any.
   *
   * E.g.,
   *
   * cropLeft("foo ; ; ; ; ; ", ";") == " ; ; ; ; "
   */
  static absl::string_view cropLeft(absl::string_view source, absl::string_view delimiters);

  /**
   * Split a delimiter-separated string view.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param keep_empty_string result contains empty strings if the string starts or ends with
   * 'split', or if instances of 'split' are adjacent; default = false.
   * @return true if found and false otherwise.
   */
  static std::vector<absl::string_view> splitToken(absl::string_view source,
                                                   absl::string_view delimiters,
                                                   bool keep_empty_string = false);

  /**
   * Size-bounded string copying and concatenation
   */
  static size_t strlcpy(char* dst, const char* src, size_t size);

  /**
   * Join elements of a vector into a string delimited by delimiter.
   * @param source supplies the strings to join.
   * @param delimiter supplies the delimiter to join them together.
   * @return string combining elements of `source` with `delimiter` in between each element.
   */
  static std::string join(const std::vector<std::string>& source, const std::string& delimiter);

  /**
   * Version of substr() that operates on a start and end index instead of a start index and a
   * length.
   */
  static std::string subspan(const std::string& source, size_t start, size_t end);

  /**
   * Escape strings for logging purposes. Returns a copy of the string with
   * \n, \r, \t, and " (double quote) escaped.
   * @param source supplies the string to escape.
   * @return escaped string.
   */
  static std::string escape(const std::string& source);

  /**
   * @return true if @param source ends with @param end.
   */
  static bool endsWith(const std::string& source, const std::string& end);

  /**
   * @param case_sensitive determines if the compare is case sensitive
   * @return true if @param source starts with @param start and ignores cases.
   */
  static bool startsWith(const char* source, const std::string& start, bool case_sensitive = true);

  /**
   * Provide a default value for a string if empty.
   * @param s string.
   * @param default_value replacement for s if empty.
   * @return s is !s.empty() otherwise default_value.
   */
  static const std::string& nonEmptyStringOrDefault(const std::string& s,
                                                    const std::string& default_value);

  /**
   * Convert a string to upper case.
   * @param s string.
   * @return std::string s converted to upper case.
   */
  static std::string toUpper(absl::string_view s);

  /**
   * Callable struct that returns the result of string comparison ignoring case.
   * @param lhs supplies the first string view.
   * @param rhs supplies the second string view.
   * @return true if strings are semantically the same and false otherwise.
   */
  struct CaseInsensitiveCompare {
    bool operator()(absl::string_view lhs, absl::string_view rhs) const;
  };

  /**
   * Callable struct that returns the hash representation of a case-insensitive string_view input.
   * @param key supplies the string view.
   * @return uint64_t hash representation of the supplied string view.
   */
  struct CaseInsensitiveHash {
    uint64_t operator()(absl::string_view key) const;
  };

  /**
   * Definition of unordered set of case-insensitive std::string.
   */
  typedef std::unordered_set<std::string, CaseInsensitiveHash, CaseInsensitiveCompare>
      CaseUnorderedSet;

  /**
   * Removes all the character indices from str contained in the interval-set.
   * @param str the string containing the characters to be removed.
   * @param remove_characters the set of character-intervals.
   * @return std::string the string with the desired characters removed.
   */
  static std::string removeCharacters(const absl::string_view& str,
                                      const IntervalSet<size_t>& remove_characters);
};

/**
 * Utilities for finding primes
 */
class Primes {
public:
  /**
   * Determines whether x is prime.
   */
  static bool isPrime(uint32_t x);

  /**
   * Finds the next prime number larger than x.
   */
  static uint32_t findPrimeLargerThan(uint32_t x);
};

/**
 * Utilities for constructing regular expressions.
 */
class RegexUtil {
public:
  /*
   * Constructs a std::regex, converting any std::regex_error exception into an EnvoyException.
   * @param regex std::string containing the regular expression to parse.
   * @param flags std::regex::flag_type containing parser flags. Defaults to std::regex::optimize.
   * @return std::regex constructed from regex and flags
   * @throw EnvoyException if the regex string is invalid.
   */
  static std::regex parseRegex(const std::string& regex,
                               std::regex::flag_type flags = std::regex::optimize);
};

/**
 * Maintains sets of numeric intervals. As new intervals are added, existing ones in the
 * set are combined so that no overlapping intervals remain in the representation.
 *
 * Value can be any type that is comparable with <, ==, and >.
 */
template <typename Value> class IntervalSetImpl : public IntervalSet<Value> {
public:
  // Interval is a pair of Values.
  typedef typename IntervalSet<Value>::Interval Interval;

  void insert(Value left, Value right) override {
    if (left == right) {
      return;
    }
    ASSERT(left < right);

    // There 3 cases where we'll decide the [left, right) is disjoint with the
    // current contents, and just need to insert. But we'll structure the code
    // to search for where existing interval(s) needs to be merged, and fall back
    // to the disjoint insertion case.
    if (!intervals_.empty()) {
      const auto left_pos = intervals_.lower_bound(Interval(left, left));
      if (left_pos != intervals_.end() && (right >= left_pos->first)) {
        // upper_bound is exclusive, and we want to be inclusive.
        auto right_pos = intervals_.upper_bound(Interval(right, right));
        if (right_pos != intervals_.begin()) {
          --right_pos;
          if (right_pos->second >= left) {
            // Both bounds overlap, with one or more existing intervals.
            left = std::min(left_pos->first, left);
            right = std::max(right_pos->second, right);
            ++right_pos; // erase is non-inclusive on upper bound.
            intervals_.erase(left_pos, right_pos);
          }
        }
      }
    }
    intervals_.insert(Interval(left, right));
  }

  std::vector<Interval> toVector() const override {
    return std::vector<Interval>(intervals_.begin(), intervals_.end());
  }

  void clear() override { intervals_.clear(); }

private:
  struct Compare {
    bool operator()(const Interval& a, const Interval& b) const { return a.second < b.first; }
  };
  std::set<Interval, Compare> intervals_; // Intervals do not overlap or abut.
};

/**
 * Hashing functor for use with unordered_map and unordered_set with string_view as a key.
 */
struct StringViewHash {
  std::size_t operator()(const absl::string_view& k) const { return HashUtil::xxHash64(k); }
};

/**
 * Computes running standard-deviation using Welford's algorithm:
 * https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm
 */
class WelfordStandardDeviation {
public:
  /**
   * Accumulates a new value into the standard deviation.
   * @param newValue the new value
   */
  void update(double newValue);

  /**
   * @return double the computed mean value.
   */
  double mean() const { return mean_; }

  /**
   * @return uint64_t the number of times update() was called
   */
  uint64_t count() const { return count_; }

  /**
   * @return double the standard deviation.
   */
  double computeStandardDeviation() const;

private:
  double computeVariance() const;

  uint64_t count_{0};
  double mean_{0};
  double m2_{0};
};

} // namespace Envoy
