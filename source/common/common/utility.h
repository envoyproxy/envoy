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
 * Production implementation of SystemTimeSource that returns the current time.
 */
class ProdSystemTimeSource : public SystemTimeSource {
public:
  // SystemTimeSource
  SystemTime currentSystemTime() override { return std::chrono::system_clock::now(); }

  static ProdSystemTimeSource instance_;
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
  static int caseInsensitiveCompare(const char* lhs, const char* rhs) {
    return strcasecmp(lhs, rhs);
  }

  /**
   * Convert an unsigned integer to a base 10 string as fast as possible.
   * @param out supplies the string to fill.
   * @param out_len supplies the length of the output buffer. Must be >= 21.
   * @param i supplies the number to convert.
   * @return the size of the string, not including the null termination.
   */
  static uint32_t itoa(char* out, size_t out_len, uint64_t i);

  /**
   * Trim trailing whitespace from a string in place.
   */
  static void rtrim(std::string& source);

  /**
   * Size-bounded string copying and concatenation
   */
  static size_t strlcpy(char* dst, const char* src, size_t size);

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
   * Tokenize a string. Note that token separaters are retained in the output.
   * @param source supplies the string to split.
   * @param splitters supplies characters to split on.
   */
  static std::vector<std::string> tokenize(const std::string& source, const std::string splitters);

  /**
   * @return true if @param source ends with @param end.
   */
  static bool endsWith(const std::string& source, const std::string& end);

  /**
   * @param case_sensitive determines if the compare is case sensitive
   * @return true if @param source starts with @param start and ignores cases.
   */
  static bool startsWith(const char* source, const std::string& start, bool case_sensitive = true);
};
