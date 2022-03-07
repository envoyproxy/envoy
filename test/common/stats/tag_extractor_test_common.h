#pragma once

#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/stats/tag_extractor_impl.h"
#include "source/common/stats/tag_producer_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class DefaultTagRegexTester {
public:
  DefaultTagRegexTester() : tag_extractors_(envoy::config::metrics::v3::StatsConfig()) {}

  void testRegex(const std::string& stat_name, const std::string& expected_tag_extracted_name,
                 const TagVector& expected_tags) {

    // Test forward iteration through the regexes
    TagVector tags;
    const std::string tag_extracted_name = tag_extractors_.produceTags(stat_name, tags);

    auto cmp = [](const Tag& lhs, const Tag& rhs) {
      return lhs.name_ == rhs.name_ && lhs.value_ == rhs.value_;
    };

    EXPECT_EQ(expected_tag_extracted_name, tag_extracted_name);
    ASSERT_EQ(expected_tags.size(), tags.size())
        << fmt::format("Stat name '{}' did not produce the expected number of tags", stat_name);
    EXPECT_TRUE(std::is_permutation(expected_tags.begin(), expected_tags.end(), tags.begin(), cmp))
        << fmt::format("Stat name '{}' did not produce the expected tags", stat_name);

    // Reverse iteration through regexes to ensure ordering invariance
    TagVector rev_tags;
    const std::string rev_tag_extracted_name = produceTagsReverse(stat_name, rev_tags);

    EXPECT_EQ(expected_tag_extracted_name, rev_tag_extracted_name);
    ASSERT_EQ(expected_tags.size(), rev_tags.size())
        << fmt::format("Stat name '{}' did not produce the expected number of tags when regexes "
                       "were run in reverse order",
                       stat_name);
    EXPECT_TRUE(
        std::is_permutation(expected_tags.begin(), expected_tags.end(), rev_tags.begin(), cmp))
        << fmt::format("Stat name '{}' did not produce the expected tags when regexes were run in "
                       "reverse order",
                       stat_name);
  }

  /**
   * Reimplements TagProducerImpl::produceTags, but extracts the tags in reverse order.
   * This helps demonstrate that the order of extractors does not matter to the end result,
   * assuming we don't care about tag-order. This is in large part correct by design because
   * stat_name is not mutated until all the extraction is done.
   * @param metric_name std::string a name of Stats::Metric (Counter, Gauge, Histogram).
   * @param tags TagVector& a set of Stats::Tag.
   * @return std::string the metric_name with tags removed.
   */
  std::string produceTagsReverse(const std::string& metric_name, TagVector& tags) const {
    // TODO(jmarantz): Skip the creation of string-based tags, creating a StatNameTagVector instead.

    // Note: one discrepancy between this and TagProducerImpl::produceTags is that this
    // version does not add in tag_extractors_.default_tags_ into tags. That doesn't matter
    // for this test, however.
    std::list<const TagExtractor*> extractors; // Note push-front is used to reverse order.
    tag_extractors_.forEachExtractorMatching(metric_name,
                                             [&extractors](const TagExtractorPtr& tag_extractor) {
                                               extractors.push_front(tag_extractor.get());
                                             });

    IntervalSetImpl<size_t> remove_characters;
    TagExtractionContext tag_extraction_context(metric_name);
    for (const TagExtractor* tag_extractor : extractors) {
      tag_extractor->extractTag(tag_extraction_context, tags, remove_characters);
    }
    return StringUtil::removeCharacters(metric_name, remove_characters);
  }

  SymbolTableImpl symbol_table_;
  TagProducerImpl tag_extractors_;
};

} // namespace Stats
} // namespace Envoy
