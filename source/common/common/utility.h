#pragma once

#include "envoy/common/time.h"

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
 * Utility class for date/time helpers.
 */
class DateUtil {
public:
  /**
   * @return whether a time_point contains a valid, not default constructed time.
   */
  static bool timePointValid(SystemTime time_point);
};

/**
 * Utility routines for working with strings.
 */
class StringUtil {
public:
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
  static int caseInsensitiveCompare(const std::string& lhs, const std::string& rhs);

  /**
   * Trim trailing whitespace from a string in place.
   */
  static void rtrim(std::string& source);

  /**
   * Split a string.
   * @param source supplies the string to split.
   * @param split supplies the char to split on.
   */
  static std::vector<std::string> split(const std::string& source, char split);

  /**
   * Version of substr() that operates on a start and end index instead of a start index and a
   * length.
   */
  static std::string subspan(const std::string& source, size_t start, size_t end);

  /**
   * @return true if @param source ends with @param end.
   */
  static bool endsWith(const std::string& source, const std::string& end);

  /**
   * @param case_sensitive determines if the compare is case sensitive
   * @return true if @param source starts with @param start and ignores cases.
   */
  static bool startsWith(const std::string& source, const std::string& start,
                         bool case_sensitive = true);

  /**
   * @return original @param input string if it's not empty or @param default_value otherwise.
   */
  static const std::string& valueOrDefault(const std::string& input,
                                           const std::string& default_value);

  static std::string replaceAll(const std::string& input, const std::string& search,
                                const std::string& replacement);
};
