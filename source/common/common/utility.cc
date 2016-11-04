#include "utility.h"

std::string DateFormatter::fromTime(const SystemTime& time) {
  return fromTimeT(std::chrono::system_clock::to_time_t(time));
}

std::string DateFormatter::fromTimeT(time_t time) {
  tm current_tm;
  gmtime_r(&time, &current_tm);

  std::array<char, 1024> buf;
  strftime(&buf[0], buf.size(), format_string_.c_str(), &current_tm);
  return std::string(&buf[0]);
}

std::string DateFormatter::now() {
  time_t current_time_t;
  time(&current_time_t);
  return fromTimeT(current_time_t);
}

bool DateUtil::timePointValid(SystemTime time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
             .count() != 0;
}

bool StringUtil::atoul(const char* str, uint64_t& out, int base) {
  if (strlen(str) == 0) {
    return false;
  }

  char* end_ptr;
  out = strtoul(str, &end_ptr, base);
  if (*end_ptr != '\0' || (out == ULONG_MAX && errno == ERANGE)) {
    return false;
  } else {
    return true;
  }
}

int StringUtil::caseInsensitiveCompare(const std::string& lhs, const std::string& rhs) {
  return strcasecmp(lhs.c_str(), rhs.c_str());
}

void StringUtil::rtrim(std::string& source) {
  std::size_t pos = source.find_last_not_of(" \t\f\v\n\r");
  if (pos != std::string::npos) {
    source.erase(pos + 1);
  } else {
    source.clear();
  }
}

std::vector<std::string> StringUtil::split(const std::string& source, char split) {
  std::vector<std::string> ret;
  size_t last_index = 0;
  size_t next_index;

  do {
    next_index = source.find(split, last_index);
    if (next_index == std::string::npos) {
      next_index = source.size();
    }

    if (next_index != last_index) {
      ret.emplace_back(subspan(source, last_index, next_index));
    }

    last_index = next_index + 1;
  } while (next_index != source.size());

  return ret;
}

std::string StringUtil::subspan(const std::string& source, size_t start, size_t end) {
  return source.substr(start, end - start);
}

std::string AccessLogDateTimeFormatter::fromTime(const SystemTime& time) {
  static DateFormatter date_format("%Y-%m-%dT%H:%M:%S");

  return fmt::format(
      "{}.{:03d}Z", date_format.fromTime(time),
      std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() %
          1000);
}

bool StringUtil::endsWith(const std::string& source, const std::string& end) {
  if (source.length() < end.length()) {
    return false;
  }

  size_t start_position = source.length() - end.length();
  return std::equal(source.begin() + start_position, source.end(), end.begin());
}

bool StringUtil::startsWith(const std::string& source, const std::string& start,
                            bool case_sensitive) {
  if (case_sensitive) {
    return strncmp(source.c_str(), start.c_str(), start.size()) == 0;
  } else {
    return strncasecmp(source.c_str(), start.c_str(), start.size()) == 0;
  }
}

const std::string& StringUtil::valueOrDefault(const std::string& input,
                                              const std::string& default_value) {
  return input.empty() ? default_value : input;
}

std::string StringUtil::replaceAll(const std::string& input, const std::string& search,
                                   const std::string& replacement) {
  std::string result = input;

  size_t pos = 0;
  while ((pos = result.find(search, pos)) != std::string::npos) {
    result.replace(pos, search.length(), replacement);
    pos += replacement.length();
  }

  return result;
}
