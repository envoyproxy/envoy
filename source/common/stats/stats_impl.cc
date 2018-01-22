#include "common/stats/stats_impl.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/utility.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Stats {

namespace {

// Round val up to the next multiple of the natural alignment.
// Note: this implementation only works because 8 is a power of 2.
size_t roundUpMultipleNaturalAlignment(size_t val) {
  const size_t multiple = alignof(RawStatData);
  static_assert(multiple == 1 || multiple == 2 || multiple == 4 || multiple == 8 || multiple == 16,
                "multiple must be a power of 2 for this algorithm to work");
  return (val + multiple - 1) & ~(multiple - 1);
}

} // namespace

size_t RawStatData::size() {
  // Normally the compiler would do this, but because name_ is a flexible-array-length
  // element, the compiler can't. RawStatData is put into an array in HotRestartImpl, so
  // it's important that each element starts on the required alignment for the type.
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + nameSize());
}

size_t& RawStatData::initializeAndGetMutableMaxObjNameLength(size_t configured_size) {
  // Like CONSTRUCT_ON_FIRST_USE, but non-const so that the value can be changed by tests
  static size_t size = configured_size;
  return size;
}

void RawStatData::configure(Server::Options& options) {
  const size_t configured = options.maxObjNameLength();
  RELEASE_ASSERT(configured > 0);
  size_t max_obj_name_length = initializeAndGetMutableMaxObjNameLength(configured);

  // If this fails, it means that this function was called too late during
  // startup because things were already using this size before it was set.
  RELEASE_ASSERT(max_obj_name_length == configured);
}

void RawStatData::configureForTestsOnly(Server::Options& options) {
  const size_t configured = options.maxObjNameLength();
  initializeAndGetMutableMaxObjNameLength(configured) = configured;
}

std::string Utility::sanitizeStatsName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  return stats_name;
}

TagExtractorImpl::TagExtractorImpl(const std::string& name, const std::string& regex)
    : name_(name), regex_(RegexUtil::parseRegex(regex)) {}

TagExtractorPtr TagExtractorImpl::createTagExtractor(const std::string& name,
                                                     const std::string& regex) {

  if (name.empty()) {
    throw EnvoyException("tag_name cannot be empty");
  }

  if (!regex.empty()) {
    return TagExtractorPtr{new TagExtractorImpl(name, regex)};
  } else {
    // Look up the default for that name.
    const auto& name_regex_pairs = Config::TagNames::get().name_regex_pairs_;
    auto it = std::find_if(name_regex_pairs.begin(), name_regex_pairs.end(),
                           [&name](const std::pair<std::string, std::string>& name_regex_pair) {
                             return name == name_regex_pair.first;
                           });
    if (it != name_regex_pairs.end()) {
      return TagExtractorPtr{new TagExtractorImpl(name, it->second)};
    } else {
      throw EnvoyException(fmt::format(
          "No regex specified for tag specifier and no default regex for name: '{}'", name));
    }
  }
}

std::string TagExtractorImpl::extractTag(const std::string& tag_extracted_name,
                                         std::vector<Tag>& tags) const {
  std::smatch match;
  // The regex must match and contain one or more subexpressions (all after the first are ignored).
  if (std::regex_search(tag_extracted_name, match, regex_) && match.size() > 1) {
    // remove_subexpr is the first submatch. It represents the portion of the string to be removed.
    const auto& remove_subexpr = match[1];

    // value_subexpr is the optional second submatch. It is usually inside the first submatch
    // (remove_subexpr) to allow the expression to strip off extra characters that should be removed
    // from the string but also not necessary in the tag value ("." for example). If there is no
    // second submatch, then the value_subexpr is the same as the remove_subexpr.
    const auto& value_subexpr = match.size() > 2 ? match[2] : remove_subexpr;

    tags.emplace_back();
    Tag& tag = tags.back();
    tag.name_ = name_;
    tag.value_ = value_subexpr.str();

    // Reconstructs the tag_extracted_name without remove_subexpr.
    return std::string(match.prefix().first, remove_subexpr.first)
        .append(remove_subexpr.second, match.suffix().second);
  }
  return tag_extracted_name;
}

RawStatData* HeapRawStatDataAllocator::alloc(const std::string& name) {
  // This must be zero-initialized
  RawStatData* data = static_cast<RawStatData*>(::calloc(RawStatData::size(), 1));
  data->initialize(name);
  return data;
}

TagProducerImpl::TagProducerImpl(const envoy::api::v2::StatsConfig& config) : TagProducerImpl() {
  // To check name conflict.
  std::unordered_set<std::string> names;
  reserveResources(config);
  addDefaultExtractors(config, names);

  for (const auto& tag_specifier : config.stats_tags()) {
    if (!names.emplace(tag_specifier.tag_name()).second) {
      throw EnvoyException(fmt::format("Tag name '{}' specified twice.", tag_specifier.tag_name()));
    }

    // If no tag value is found, fallback to default regex to keep backward compatibility.
    if (tag_specifier.tag_value_case() == envoy::api::v2::TagSpecifier::TAG_VALUE_NOT_SET ||
        tag_specifier.tag_value_case() == envoy::api::v2::TagSpecifier::kRegex) {
      tag_extractors_.emplace_back(Stats::TagExtractorImpl::createTagExtractor(
          tag_specifier.tag_name(), tag_specifier.regex()));

    } else if (tag_specifier.tag_value_case() == envoy::api::v2::TagSpecifier::kFixedValue) {
      default_tags_.emplace_back(
          Stats::Tag{.name_ = tag_specifier.tag_name(), .value_ = tag_specifier.fixed_value()});
    }
  }
}

std::string TagProducerImpl::produceTags(const std::string& name, std::vector<Tag>& tags) const {
  tags.insert(tags.end(), default_tags_.begin(), default_tags_.end());

  std::string tag_extracted_name = name;
  for (const TagExtractorPtr& tag_extractor : tag_extractors_) {
    tag_extracted_name = tag_extractor->extractTag(tag_extracted_name, tags);
  }
  return tag_extracted_name;
}

// Roughly estimate the size of the vectors.
void TagProducerImpl::reserveResources(const envoy::api::v2::StatsConfig& config) {
  default_tags_.reserve(config.stats_tags().size());

  if (!config.has_use_all_default_tags() || config.use_all_default_tags().value()) {
    tag_extractors_.reserve(Config::TagNames::get().name_regex_pairs_.size() +
                            config.stats_tags().size());
  } else {
    tag_extractors_.reserve(config.stats_tags().size());
  }
}

void TagProducerImpl::addDefaultExtractors(const envoy::api::v2::StatsConfig& config,
                                           std::unordered_set<std::string>& names) {
  if (!config.has_use_all_default_tags() || config.use_all_default_tags().value()) {
    for (const auto& extractor : Config::TagNames::get().name_regex_pairs_) {
      names.emplace(extractor.first);
      tag_extractors_.emplace_back(
          Stats::TagExtractorImpl::createTagExtractor(extractor.first, extractor.second));
    }
  }
}

void HeapRawStatDataAllocator::free(RawStatData& data) {
  // This allocator does not ever have concurrent access to the raw data.
  ASSERT(data.ref_count_ == 1);
  ::free(&data);
}

void RawStatData::initialize(absl::string_view key) {
  ASSERT(!initialized());
  ASSERT(key.size() <= maxNameLength());
  ASSERT(absl::string_view::npos == key.find(':'));
  ref_count_ = 1;

  // key is not necessarily nul-terminated, but we want to make sure name_ is.
  size_t xfer_size = std::min(nameSize() - 1, key.size());
  memcpy(name_, key.data(), xfer_size);
  name_[xfer_size] = '\0';
}

} // namespace Stats
} // namespace Envoy
