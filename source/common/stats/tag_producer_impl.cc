#include "source/common/stats/tag_producer_impl.h"

#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/utility.h"
#include "source/common/stats/tag_extractor_impl.h"

namespace Envoy {
namespace Stats {

TagProducerImpl::TagProducerImpl(const envoy::config::metrics::v3::StatsConfig& config) {
  // To check name conflict.
  reserveResources(config);
  absl::node_hash_set<std::string> names = addDefaultExtractors(config);

  for (const auto& tag_specifier : config.stats_tags()) {
    const std::string& name = tag_specifier.tag_name();
    if (!names.emplace(name).second) {
      throw EnvoyException(fmt::format("Tag name '{}' specified twice.", name));
    }

    // If no tag value is found, fallback to default regex to keep backward compatibility.
    if (tag_specifier.tag_value_case() ==
            envoy::config::metrics::v3::TagSpecifier::TagValueCase::TAG_VALUE_NOT_SET ||
        tag_specifier.tag_value_case() ==
            envoy::config::metrics::v3::TagSpecifier::TagValueCase::kRegex) {

      if (tag_specifier.regex().empty()) {
        if (addExtractorsMatching(name) == 0) {
          throw EnvoyException(fmt::format(
              "No regex specified for tag specifier and no default regex for name: '{}'", name));
        }
      } else {
        addExtractor(TagExtractorImplBase::createTagExtractor(name, tag_specifier.regex()));
      }
    } else if (tag_specifier.tag_value_case() ==
               envoy::config::metrics::v3::TagSpecifier::TagValueCase::kFixedValue) {
      default_tags_.emplace_back(Tag{name, tag_specifier.fixed_value()});
    }
  }
}

int TagProducerImpl::addExtractorsMatching(absl::string_view name) {
  int num_found = 0;
  for (const auto& desc : Config::TagNames::get().descriptorVec()) {
    if (desc.name_ == name) {
      addExtractor(TagExtractorImplBase::createTagExtractor(desc.name_, desc.regex_, desc.substr_,
                                                            desc.re_type_));
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
  tags.insert(tags.end(), default_tags_.begin(), default_tags_.end());
  IntervalSetImpl<size_t> remove_characters;
  TagExtractionContext tag_extraction_context(metric_name);
  std::vector<absl::string_view> tokens;
  forEachExtractorMatching(metric_name, [&remove_characters, &tags, &tag_extraction_context](
                                            const TagExtractorPtr& tag_extractor) {
    tag_extractor->extractTag(tag_extraction_context, tags, remove_characters);
  });
  return StringUtil::removeCharacters(metric_name, remove_characters);
}

void TagProducerImpl::reserveResources(const envoy::config::metrics::v3::StatsConfig& config) {
  default_tags_.reserve(config.stats_tags().size());
}

absl::node_hash_set<std::string>
TagProducerImpl::addDefaultExtractors(const envoy::config::metrics::v3::StatsConfig& config) {
  absl::node_hash_set<std::string> names;
  if (!config.has_use_all_default_tags() || config.use_all_default_tags().value()) {
    for (const auto& desc : Config::TagNames::get().descriptorVec()) {
      names.emplace(desc.name_);
      addExtractor(TagExtractorImplBase::createTagExtractor(desc.name_, desc.regex_, desc.substr_,
                                                            desc.re_type_));
    }
    for (const auto& desc : Config::TagNames::get().tokenizedDescriptorVec()) {
      names.emplace(desc.name_);
      addExtractor(std::make_unique<TagExtractorTokensImpl>(desc.name_, desc.pattern_));
    }
  }
  return names;
}

} // namespace Stats
} // namespace Envoy
