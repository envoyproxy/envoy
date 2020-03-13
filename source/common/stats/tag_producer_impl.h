#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/tag_extractor.h"
#include "envoy/stats/tag_producer.h"

#include "common/common/hash.h"
#include "common/common/utility.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Organizes a collection of TagExtractors so that stat-names can be processed without
 * iterating through all extractors.
 */
class TagProducerImpl : public TagProducer {
public:
  TagProducerImpl(const envoy::config::metrics::v3::StatsConfig& config);
  TagProducerImpl() = default;

  /**
   * Take a metric name and a vector then add proper tags into the vector and
   * return an extracted metric name.
   * @param metric_name std::string a name of Stats::Metric (Counter, Gauge, Histogram).
   * @param tags std::vector a set of Stats::Tag.
   */
  std::string produceTags(absl::string_view metric_name, TagVector& tags) const override;

private:
  friend class DefaultTagRegexTester;

  /**
   * Adds a TagExtractor to the collection of tags, tracking prefixes to help make
   * produceTags run efficiently by trying only extractors that have a chance to match.
   * @param extractor TagExtractorPtr the extractor to add.
   */
  void addExtractor(TagExtractorPtr extractor);

  /**
   * Adds all default extractors matching the specified tag name. In this model,
   * more than one TagExtractor can be used to generate a given tag. The default
   * extractors are specified in common/config/well_known_names.cc.
   * @param name absl::string_view the extractor to add.
   * @return int the number of matching extractors.
   */
  int addExtractorsMatching(absl::string_view name);

  /**
   * Roughly estimate the size of the vectors.
   * @param config const envoy::config::metrics::v2::StatsConfig& the config.
   */
  void reserveResources(const envoy::config::metrics::v3::StatsConfig& config);

  /**
   * Adds all default extractors from well_known_names.cc into the
   * collection. Returns a set of names of all default extractors
   * into a string-set for dup-detection against new stat names
   * specified in the configuration.
   * @param config const envoy::config::metrics::v2::StatsConfig& the config.
   * @return names std::unordered_set<std::string> the set of names to populate
   */
  std::unordered_set<std::string>
  addDefaultExtractors(const envoy::config::metrics::v3::StatsConfig& config);

  /**
   * Iterates over every tag extractor that might possibly match stat_name, calling
   * callback f for each one. This is broken out this way to reduce code redundancy
   * during testing, where we want to verify that extraction is order-independent.
   * The possibly-matching-extractors list is computed by:
   *   1. Finding the first '.' separated token in stat_name.
   *   2. Collecting the TagExtractors whose regexes have that same prefix "^prefix\\."
   *   3. Collecting also the TagExtractors whose regexes don't start with any prefix.
   * In the future, we may also do substring searches in some cases.
   * See DefaultTagRegexTester::produceTagsReverse in test/common/stats/stats_impl_test.cc.
   *
   * @param stat_name const std::string& the stat name.
   * @param f std::function<void(const TagExtractorPtr&)> function to call for each extractor.
   */
  void forEachExtractorMatching(absl::string_view stat_name,
                                std::function<void(const TagExtractorPtr&)> f) const;

  std::vector<TagExtractorPtr> tag_extractors_without_prefix_;

  // Maps a prefix word extracted out of a regex to a vector of TagExtractors. Note that
  // the storage for the prefix string is owned by the TagExtractor, which, depending on
  // implementation, may need make a copy of the prefix.
  absl::flat_hash_map<absl::string_view, std::vector<TagExtractorPtr>> tag_extractor_prefix_map_;
  TagVector default_tags_;
};

} // namespace Stats
} // namespace Envoy
