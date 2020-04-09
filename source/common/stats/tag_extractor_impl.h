#pragma once

#include <cstdint>
#include <regex>
#include <string>

#include "envoy/stats/tag_extractor.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class TagExtractorImpl : public TagExtractor {
public:
  /**
   * Creates a tag extractor from the regex provided. name and regex must be non-empty.
   * @param name name for tag extractor.
   * @param regex regex expression.
   * @param substr a substring that -- if provided -- must be present in a stat name
   *               in order to match the regex. This is an optional performance tweak
   *               to avoid large numbers of failed regex lookups.
   * @return TagExtractorPtr newly constructed TagExtractor.
   */
  static TagExtractorPtr createTagExtractor(const std::string& name, const std::string& regex,
                                            const std::string& substr = "");

  TagExtractorImpl(const std::string& name, const std::string& regex,
                   const std::string& substr = "");
  std::string name() const override { return name_; }
  bool extractTag(absl::string_view tag_extracted_name, TagVector& tags,
                  IntervalSet<size_t>& remove_characters) const override;
  absl::string_view prefixToken() const override { return prefix_; }

  /**
   * @param stat_name The stat name
   * @return bool indicates whether tag extraction should be skipped for this stat_name due
   * to a substring mismatch.
   */
  bool substrMismatch(absl::string_view stat_name) const;

private:
  /**
   * Examines a regex string, looking for the pattern: ^alphanumerics_with_underscores\.
   * Returns "alphanumerics_with_underscores" if that pattern is found, empty-string otherwise.
   * @param regex absl::string_view the regex to scan for prefixes.
   * @return std::string the prefix, or "" if no prefix found.
   */
  static std::string extractRegexPrefix(absl::string_view regex);
  const std::string name_;
  const std::string prefix_;
  const std::string substr_;
  const std::regex regex_;
};

} // namespace Stats
} // namespace Envoy
