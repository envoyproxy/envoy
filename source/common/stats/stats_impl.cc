#include "common/stats/stats_impl.h"

#include <string.h>

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
  // element, the compiler can't.  RawStatData is put into an array in HotRestartImpl, so
  // it's important that each element starts on the required alignment for the type.
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + nameSize());
}

size_t& RawStatData::initializeAndGetMutableMaxNameLength(size_t configured_size) {
  // Like CONSTRUCT_ON_FIRST_USE, but non-const so that the value can be changed by tests
  static size_t size = configured_size;
  return size;
}

void RawStatData::configure(Server::Options& options) {
  const size_t configured = options.maxStatNameLength();
  RELEASE_ASSERT(configured > 0);
  size_t max_name_length = initializeAndGetMutableMaxNameLength(configured);

  // If this fails, it means that this function was called too late during
  // startup because things were already using this size before it was set.
  RELEASE_ASSERT(max_name_length == configured);
}

void RawStatData::configureForTestsOnly(Server::Options& options) {
  const size_t configured = options.maxStatNameLength();
  initializeAndGetMutableMaxNameLength(configured) = configured;
}

std::string Utility::sanitizeStatsName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  return stats_name;
}

TagExtractorImpl::TagExtractorImpl(const std::string& name, const std::string& regex)
    : name_(name), regex_(regex) {}

TagExtractorPtr TagExtractorImpl::createTagExtractor(const std::string& name,
                                                     const std::string& regex) {

  if (name.empty()) {
    throw EnvoyException("tag_name cannot be empty");
  }

  if (!regex.empty()) {
    return TagExtractorPtr{new TagExtractorImpl(name, regex)};
  } else {
    // Look up the default for that name.
    const auto tag_names = Config::TagNames::get();
    auto it = tag_names.regex_map_.find(name);
    if (it != tag_names.regex_map_.end()) {
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
    const auto& remove_subexpr = match[1];
    const auto& value_subexpr = match.size() > 2 ? match[2] : remove_subexpr;

    tags.emplace_back();
    Tag& tag = tags.back();
    tag.name_ = name_;
    tag.value_ = value_subexpr.str();

    // This call invalidates match and all derived objects because they contain references to
    // tag_extracted_name.
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

void HeapRawStatDataAllocator::free(RawStatData& data) {
  // This allocator does not ever have concurrent access to the raw data.
  ASSERT(data.ref_count_ == 1);
  ::free(&data);
}

void RawStatData::initialize(const std::string& name) {
  ASSERT(!initialized());
  ASSERT(name.size() <= maxNameLength());
  ASSERT(std::string::npos == name.find(':'));
  ref_count_ = 1;
  StringUtil::strlcpy(name_, name.substr(0, maxNameLength()).c_str(), nameSize());
}

bool RawStatData::matches(const std::string& name) {
  // In case a stat got truncated, match on the truncated name.
  return 0 == strcmp(name.substr(0, maxNameLength()).c_str(), name_);
}

} // namespace Stats
} // namespace Envoy
