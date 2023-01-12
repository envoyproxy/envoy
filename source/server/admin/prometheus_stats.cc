#include "source/server/admin/prometheus_stats.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"
#include "source/common/common/regex.h"
#include "source/common/stats/histogram_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Server {

namespace {

const Regex::CompiledGoogleReMatcher& promRegex() {
  CONSTRUCT_ON_FIRST_USE(Regex::CompiledGoogleReMatcher, "[^a-zA-Z0-9_]", false);
}

/**
 * Take a string and sanitize it according to Prometheus conventions.
 */
std::string sanitizeName(const absl::string_view name) {
  // The name must match the regex [a-zA-Z_][a-zA-Z0-9_]* as required by
  // prometheus. Refer to https://prometheus.io/docs/concepts/data_model/.
  // The initial [a-zA-Z_] constraint is always satisfied by the namespace prefix.
  return promRegex().replaceAll(name, "_");
}

/**
 * Take tag values and sanitize it for text serialization, according to
 * Prometheus conventions.
 */
std::string sanitizeValue(const absl::string_view value) {
  // Removes problematic characters from Prometheus tag values to prevent
  // text serialization issues. This matches the prometheus text formatting code:
  // https://github.com/prometheus/common/blob/88f1636b699ae4fb949d292ffb904c205bf542c9/expfmt/text_create.go#L419-L420.
  // The goal is to replace '\' with "\\", newline with "\n", and '"' with "\"".
  return absl::StrReplaceAll(value, {
                                        {R"(\)", R"(\\)"},
                                        {"\n", R"(\n)"},
                                        {R"(")", R"(\")"},
                                    });
}

} // namespace

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), sanitizeValue(tag.value_)));
  }
  return absl::StrJoin(buf, ",");
}

absl::optional<std::string>
PrometheusStatsFormatter::metricName(const std::string& extracted_name,
                                     const Stats::CustomStatNamespaces& custom_namespaces) {
  const absl::optional<absl::string_view> custom_namespace_stripped =
      custom_namespaces.stripRegisteredPrefix(extracted_name);
  if (custom_namespace_stripped.has_value()) {
    // This case the name has a custom namespace, and it is a custom metric.
    const std::string sanitized_name = sanitizeName(custom_namespace_stripped.value());
    // We expose these metrics without modifying (e.g. without "envoy_"),
    // so we have to check the "user-defined" stat name complies with the Prometheus naming
    // convention. Specifically the name must start with the "[a-zA-Z_]" pattern.
    // All the characters in sanitized_name are already in "[a-zA-Z0-9_]" pattern
    // thanks to sanitizeName above, so the only thing we have to do is check
    // if it does not start with digits.
    if (sanitized_name.empty() || absl::ascii_isdigit(sanitized_name.front())) {
      return absl::nullopt;
    }
    return sanitized_name;
  }

  // If it does not have a custom namespace, add namespacing prefix to avoid conflicts, as per best
  // practice: https://prometheus.io/docs/practices/naming/#metric-names Also, naming conventions on
  // https://prometheus.io/docs/concepts/data_model/
  return absl::StrCat("envoy_", sanitizeName(extracted_name));
}

} // namespace Server
} // namespace Envoy
