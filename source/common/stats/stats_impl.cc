#include "common/stats/stats_impl.h"

#include <string.h>

#include <chrono>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/utility.h"
#include "common/config/well_known_names.h"

namespace Envoy {
namespace Stats {

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

void TagExtractorImpl::updateTags(std::string& tag_extracted_name, std::vector<Tag>& tags) const {
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
    tag_extracted_name = std::string(match.prefix().first, remove_subexpr.first)
                             .append(remove_subexpr.second, match.suffix().second);
  }
}

void TimerImpl::recordDuration(std::chrono::milliseconds ms) {
  parent_.deliverTimingToSinks(*this, ms);
}

void TimerImpl::TimespanImpl::complete(const std::string& dynamic_name) {
  std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_);
  parent_.parent_.timer(dynamic_name).recordDuration(ms);
}

RawStatData* HeapRawStatDataAllocator::alloc(const std::string& name) {
  RawStatData* data = new RawStatData();
  memset(data, 0, sizeof(RawStatData));
  data->initialize(name);
  return data;
}

void HeapRawStatDataAllocator::free(RawStatData& data) {
  // This allocator does not ever have concurrent access to the raw data.
  ASSERT(data.ref_count_ == 1);
  delete &data;
}

void RawStatData::initialize(const std::string& name) {
  ASSERT(!initialized());
  ASSERT(name.size() <= MAX_NAME_SIZE);
  ASSERT(std::string::npos == name.find(':'));
  ref_count_ = 1;
  StringUtil::strlcpy(name_, name.substr(0, MAX_NAME_SIZE).c_str(), MAX_NAME_SIZE + 1);
}

bool RawStatData::matches(const std::string& name) {
  // In case a stat got truncated, match on the truncated name.
  return 0 == strcmp(name.substr(0, MAX_NAME_SIZE).c_str(), name_);
}

} // namespace Stats
} // namespace Envoy
