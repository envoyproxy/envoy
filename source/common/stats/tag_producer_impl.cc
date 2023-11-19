#include "source/common/stats/tag_producer_impl.h"

#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/utility.h"
#include "source/common/stats/tag_extractor_impl.h"

namespace Envoy {
namespace Stats {

TagProducerImpl::TagProducerImpl(const envoy::config::metrics::v3::StatsConfig& config)
    : TagProducerImpl(config, {}) {}

TagProducerImpl::TagProducerImpl(const envoy::config::metrics::v3::StatsConfig& config,
                                 const Stats::TagVector& cli_tags) {
  reserveResources(config);
  addDefaultExtractors(config);

  for (const auto& cli_tag : cli_tags) {
    addExtractor(std::make_unique<TagExtractorFixedImpl>(cli_tag.name_, cli_tag.value_));
    fixed_tags_.push_back(cli_tag);
  }

  for (const auto& tag_specifier : config.stats_tags()) {
    const std::string& name = tag_specifier.tag_name();

    // If no tag value is found, fallback to default regex to keep backward compatibility.
    if (tag_specifier.tag_value_case() ==
            envoy::config::metrics::v3::TagSpecifier::TagValueCase::TAG_VALUE_NOT_SET ||
        tag_specifier.tag_value_case() ==
            envoy::config::metrics::v3::TagSpecifier::TagValueCase::kRegex) {

      if (tag_specifier.regex().empty()) {
        if (addExtractorsMatching(name) == 0) {
          throwEnvoyExceptionOrPanic(fmt::format(
              "No regex specified for tag specifier and no default regex for name: '{}'", name));
        }
      } else {
        addExtractor(TagExtractorImplBase::createTagExtractor(name, tag_specifier.regex()));
      }
    } else if (tag_specifier.tag_value_case() ==
               envoy::config::metrics::v3::TagSpecifier::TagValueCase::kFixedValue) {
      addExtractor(std::make_unique<TagExtractorFixedImpl>(name, tag_specifier.fixed_value()));
      fixed_tags_.push_back(Tag{name, tag_specifier.fixed_value()});
    }
  }
}

int TagProducerImpl::addExtractorsMatching(absl::string_view name) {
  int num_found = 0;
  for (const auto& desc : Config::TagNames::get().descriptorVec()) {
    if (desc.name_ == name) {
      addExtractor(TagExtractorImplBase::createTagExtractor(desc.name_, desc.regex_, desc.substr_,
                                                            desc.negative_match_, desc.re_type_));
      ++num_found;
    }
  }
  for (const auto& desc : Config::TagNames::get().tokenizedDescriptorVec()) {
    if (desc.name_ == name) {
      addExtractor(std::make_unique<TagExtractorTokensImpl>(desc.name_, desc.pattern_));
      ++num_found;
    }
  }
  return num_found;
}

void TagProducerImpl::addExtractor(TagExtractorPtr extractor) {
  auto insertion = extractor_map_.insert(std::make_pair(extractor->name(), std::ref(*extractor)));
  if (!insertion.second) {
    extractor->setOtherExtractorWithSameNameExists(true);
    std::reference_wrapper<TagExtractor> other = insertion.first->second;
    other.get().setOtherExtractorWithSameNameExists(true);
  }

  const absl::string_view prefix = extractor->prefixToken();
  if (prefix.empty()) {
    tag_extractors_without_prefix_.emplace_back(std::move(extractor));
  } else {
    tag_extractor_prefix_map_[prefix].emplace_back(std::move(extractor));
  }
}

void TagProducerImpl::forEachExtractorMatching(
    absl::string_view stat_name, std::function<void(const TagExtractorPtr&)> f) const {
  IntervalSetImpl<size_t> remove_characters;
  for (const TagExtractorPtr& tag_extractor : tag_extractors_without_prefix_) {
    f(tag_extractor);
  }
  const absl::string_view::size_type dot = stat_name.find('.');
  if (dot != std::string::npos) {
    const absl::string_view token = absl::string_view(stat_name.data(), dot);
    const auto iter = tag_extractor_prefix_map_.find(token);
    if (iter != tag_extractor_prefix_map_.end()) {
      for (const TagExtractorPtr& tag_extractor : iter->second) {
        f(tag_extractor);
      }
    }
  }
}

std::string TagProducerImpl::produceTags(absl::string_view metric_name, TagVector& tags) const {
  // TODO(jmarantz): Skip the creation of string-based tags, creating a StatNameTagVector instead.
  IntervalSetImpl<size_t> remove_characters;
  TagExtractionContext tag_extraction_context(metric_name);
  std::vector<absl::string_view> tokens;
  absl::flat_hash_set<absl::string_view> dup_set;
  forEachExtractorMatching(metric_name, [&remove_characters, &tags, &tag_extraction_context,
                                         &dup_set](const TagExtractorPtr& tag_extractor) {
    // It is relatively cheap to populate a set of string_view for every tag,
    // but it saves 2% CPU time to only populate and check dup_set for tag-names
    // where there is more than one extractor. This is rare. For built-in
    // extractors this only occurs with HTTP_CONN_MANAGER_PREFIX. Istio/Wasm
    // add configuration for an alternate pattern for RESPONSE_CODE.
    bool other_extractor_with_same_name_exists = tag_extractor->otherExtractorWithSameNameExists();
    if (other_extractor_with_same_name_exists &&
        dup_set.find(tag_extractor->name()) != dup_set.end()) {
      ENVOY_LOG_EVERY_POW_2_MISC(warn, "Skipping duplicate tag for ", tag_extractor->name());
      return;
    }
    if (tag_extractor->extractTag(tag_extraction_context, tags, remove_characters) &&
        other_extractor_with_same_name_exists) {
      dup_set.insert(tag_extractor->name());
    }
  });
  return StringUtil::removeCharacters(metric_name, remove_characters);
}

void TagProducerImpl::reserveResources(const envoy::config::metrics::v3::StatsConfig& config) {
  tag_extractors_without_prefix_.reserve(config.stats_tags().size());
}

void TagProducerImpl::addDefaultExtractors(const envoy::config::metrics::v3::StatsConfig& config) {
  if (!config.has_use_all_default_tags() || config.use_all_default_tags().value()) {
    for (const auto& desc : Config::TagNames::get().descriptorVec()) {
      addExtractor(TagExtractorImplBase::createTagExtractor(desc.name_, desc.regex_, desc.substr_,
                                                            desc.negative_match_, desc.re_type_));
    }
    for (const auto& desc : Config::TagNames::get().tokenizedDescriptorVec()) {
      addExtractor(std::make_unique<TagExtractorTokensImpl>(desc.name_, desc.pattern_));
    }
  }
}

} // namespace Stats
} // namespace Envoy
