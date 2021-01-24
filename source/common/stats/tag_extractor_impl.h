#pragma once

#include <cstdint>
#include <regex>
#include <string>

#ifdef ENVOY_PERF_ANNOTATION
#include <fmt/core.h>
#endif

#include "envoy/stats/tag_extractor.h"

#include "common/common/regex.h"

#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace Envoy {
namespace Stats {

// To check if a tag extractor is actually used you can run
// bazel test //test/... --test_output=streamed --define=perf_annotation=enabled
#ifdef ENVOY_PERF_ANNOTATION

struct Counters {
  uint32_t skipped_{};
  uint32_t matched_{};
  uint32_t missed_{};
};

#define PERF_TAG_COUNTERS std::unique_ptr<Counters> counters_

#define PERF_TAG_INIT counters_ = std::make_unique<Counters>()
#define PERF_TAG_INC(member) ++(counters_->member)

#else

#define PERF_TAG_COUNTERS
#define PERF_TAG_INIT
#define PERF_TAG_INC(member)

#endif

class TagExtractorImplBase : public TagExtractor {
public:
  /**
   * Creates a tag extractor from the regex provided. name and regex must be non-empty.
   * @param name name for tag extractor.
   * @param regex regex expression.
   * @param substr a substring that -- if provided -- must be present in a stat name
   *               in order to match the regex. This is an optional performance tweak
   *               to avoid large numbers of failed regex lookups.
   * @param re_type the regular expression syntax used (Regex::Type::StdRegex or Regex::Type::Re2).
   * @return TagExtractorPtr newly constructed TagExtractor.
   */
  static TagExtractorPtr createTagExtractor(absl::string_view name, absl::string_view regex,
                                            absl::string_view substr = "",
                                            Regex::Type re_type = Regex::Type::StdRegex);

  TagExtractorImplBase(absl::string_view name, absl::string_view regex,
                       absl::string_view substr = "");
#ifdef ENVOY_PERF_ANNOTATION
  ~TagExtractorImplBase() override {
    std::cout << fmt::format("TagStats for {} tag extractor: skipped {}, matched {}, missing {}",
                             name_, counters_->skipped_, counters_->matched_, counters_->missed_)
              << std::endl;
  }
#endif
  std::string name() const override { return name_; }
  absl::string_view prefixToken() const override { return prefix_; }

  /**
   * @param stat_name The stat name
   * @return bool indicates whether tag extraction should be skipped for this stat_name due
   * to a substring mismatch.
   */
  bool substrMismatch(absl::string_view stat_name) const;

protected:
  /**
   * Examines a regex string, looking for the pattern: ^alphanumerics_with_underscores\.
   * Returns "alphanumerics_with_underscores" if that pattern is found, empty-string otherwise.
   * @param regex absl::string_view the regex to scan for prefixes.
   * @return std::string the prefix, or "" if no prefix found.
   */
  static std::string extractRegexPrefix(absl::string_view regex);

  /**
   * Adds a new tag for the current name, returning a reference to the tag value.
   *
   * @param tags the list of tags
   * @return a reference to the value of the tag that was added.
   */
  std::string& addTag(std::vector<Tag>& tags) const;

  const std::string name_;
  const std::string prefix_;
  const std::string substr_;

  PERF_TAG_COUNTERS;
};

class TagExtractorStdRegexImpl : public TagExtractorImplBase {
public:
  TagExtractorStdRegexImpl(absl::string_view name, absl::string_view regex,
                           absl::string_view substr);

  bool extractTag(absl::string_view tag_extracted_name, std::vector<Tag>& tags,
                  IntervalSet<size_t>& remove_characters) const override;

private:
  const std::regex regex_;
};

class TagExtractorRe2Impl : public TagExtractorImplBase {
public:
  TagExtractorRe2Impl(absl::string_view name, absl::string_view regex,
                      absl::string_view substr);

  bool extractTag(absl::string_view tag_extracted_name, std::vector<Tag>& tags,
                  IntervalSet<size_t>& remove_characters) const override;

private:
  const re2::RE2 regex_;
};

/*class TagExtractorSymbolicImpl : public TagExtractorImplBase {
public:
  TagExtractorSymbolicImpl(absl::string_view name, absl::string_view regex,
                           absl::string_view substr = "");

  bool extractTag(absl::string_view tag_extracted_name, std::vector<Tag>& tags,
                  IntervalSet<size_t>& remove_characters) const override;

private:
  StatNamePool pool_;
  const re2::RE2 regex_;
  };*/

} // namespace Stats
} // namespace Envoy
