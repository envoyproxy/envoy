#include "source/extensions/filters/http/orca_reporter/cgroup_stats.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "source/common/common/fmt.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

namespace {

bool isValidEnvName(absl::string_view name) {
  if (name.empty() || !(absl::ascii_isalpha(name[0]) || name[0] == '_')) {
    return false;
  }
  for (const char c : name) {
    if (!(absl::ascii_isalnum(c) || c == '_')) {
      return false;
    }
  }
  return true;
}

// Returns the value of the "<key> <value>" line in a flat keyed file such as cpu.stat. For
// example, key "usage_usec" in "usage_usec 1000\nuser_usec 600\n" returns 1000.
absl::StatusOr<uint64_t> parseKeyedValue(absl::string_view content, absl::string_view key) {
  for (const absl::string_view line : absl::StrSplit(content, '\n', absl::SkipWhitespace())) {
    const std::pair<absl::string_view, absl::string_view> entry =
        absl::StrSplit(line, absl::MaxSplits(' ', 1));
    if (entry.first != key) {
      continue;
    }
    uint64_t value;
    if (!absl::SimpleAtoi(entry.second, &value)) {
      return absl::InvalidArgumentError(fmt::format("invalid value for '{}': '{}'", key, line));
    }
    return value;
  }
  return absl::NotFoundError(fmt::format("'{}' not found", key));
}

} // namespace

std::optional<std::string> processEnvLookup(const std::string& name) {
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

absl::StatusOr<std::string> resolvePathTemplate(absl::string_view path_template,
                                                absl::Span<const Matcher::RegexReplace> rewrites,
                                                const EnvLookup& env_lookup) {
  std::string result;
  size_t pos = 0;
  while (pos < path_template.size()) {
    const size_t open = path_template.find('{', pos);
    if (open == absl::string_view::npos) {
      absl::StrAppend(&result, path_template.substr(pos));
      break;
    }
    absl::StrAppend(&result, path_template.substr(pos, open - pos));

    const size_t close = path_template.find('}', open + 1);
    if (close == absl::string_view::npos) {
      return absl::InvalidArgumentError(
          fmt::format("unterminated placeholder in cgroup path '{}'", path_template));
    }
    const std::string name(path_template.substr(open + 1, close - open - 1));
    if (!isValidEnvName(name)) {
      return absl::InvalidArgumentError(
          fmt::format("invalid placeholder '{{{}}}' in cgroup path '{}'", name, path_template));
    }

    std::optional<std::string> value = env_lookup(name);
    if (!value.has_value() || value->empty()) {
      return absl::InvalidArgumentError(
          fmt::format("environment variable '{}' used in cgroup path '{}' is not set or empty",
                      name, path_template));
    }
    std::string rewritten = std::move(value).value();
    for (const Matcher::RegexReplace& rewrite : rewrites) {
      rewritten = rewrite.apply(rewritten);
    }
    absl::StrAppend(&result, rewritten);
    pos = close + 1;
  }
  return result;
}

absl::StatusOr<uint64_t> parseCpuStatUsageUsec(absl::string_view cpu_stat) {
  return parseKeyedValue(cpu_stat, "usage_usec");
}

absl::StatusOr<std::optional<double>> parseCpuMax(absl::string_view cpu_max) {
  const std::vector<absl::string_view> fields =
      absl::StrSplit(absl::StripAsciiWhitespace(cpu_max), ' ', absl::SkipEmpty());
  if (fields.size() != 2) {
    return absl::InvalidArgumentError(fmt::format("invalid cpu.max: '{}'", cpu_max));
  }
  if (fields[0] == "max") {
    return std::nullopt;
  }
  uint64_t quota;
  uint64_t period;
  if (!absl::SimpleAtoi(fields[0], &quota) || !absl::SimpleAtoi(fields[1], &period) || quota == 0 ||
      period == 0) {
    return absl::InvalidArgumentError(fmt::format("invalid cpu.max: '{}'", cpu_max));
  }
  return static_cast<double>(quota) / static_cast<double>(period);
}

absl::StatusOr<uint64_t> parseSingleValue(absl::string_view content) {
  uint64_t value;
  if (!absl::SimpleAtoi(absl::StripAsciiWhitespace(content), &value)) {
    return absl::InvalidArgumentError(fmt::format("invalid value: '{}'", content));
  }
  return value;
}

absl::StatusOr<std::optional<uint64_t>> parseMemoryMax(absl::string_view memory_max) {
  if (absl::StripAsciiWhitespace(memory_max) == "max") {
    return std::nullopt;
  }
  absl::StatusOr<uint64_t> value = parseSingleValue(memory_max);
  RETURN_IF_NOT_OK_REF(value.status());
  if (value.value() == 0) {
    return absl::InvalidArgumentError("memory.max is 0");
  }
  return value.value();
}

absl::StatusOr<uint64_t> parseMemoryStatInactiveFile(absl::string_view memory_stat) {
  return parseKeyedValue(memory_stat, "inactive_file");
}

absl::StatusOr<std::string> CgroupReader::read(absl::string_view file) const {
  const std::string path = absl::StrCat(directory_, "/", file);
  absl::StatusOr<std::string> content = fs_.fileReadToEnd(path);
  if (!content.ok()) {
    return absl::UnavailableError(
        fmt::format("unable to read {}: {}", path, content.status().message()));
  }
  return content;
}

absl::StatusOr<uint64_t> CgroupReader::cpuUsageUsec() const {
  absl::StatusOr<std::string> content = read("cpu.stat");
  RETURN_IF_NOT_OK_REF(content.status());
  return parseCpuStatUsageUsec(content.value());
}

absl::StatusOr<std::optional<double>> CgroupReader::cpuLimitCores() const {
  absl::StatusOr<std::string> content = read("cpu.max");
  RETURN_IF_NOT_OK_REF(content.status());
  return parseCpuMax(content.value());
}

absl::StatusOr<uint64_t> CgroupReader::memoryWorkingSetBytes() const {
  absl::StatusOr<std::string> current_content = read("memory.current");
  RETURN_IF_NOT_OK_REF(current_content.status());
  absl::StatusOr<uint64_t> current = parseSingleValue(current_content.value());
  RETURN_IF_NOT_OK_REF(current.status());

  absl::StatusOr<std::string> stat_content = read("memory.stat");
  RETURN_IF_NOT_OK_REF(stat_content.status());
  absl::StatusOr<uint64_t> inactive_file = parseMemoryStatInactiveFile(stat_content.value());
  RETURN_IF_NOT_OK_REF(inactive_file.status());

  return current.value() - std::min(current.value(), inactive_file.value());
}

absl::StatusOr<std::optional<uint64_t>> CgroupReader::memoryLimitBytes() const {
  absl::StatusOr<std::string> content = read("memory.max");
  RETURN_IF_NOT_OK_REF(content.status());
  return parseMemoryMax(content.value());
}

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
