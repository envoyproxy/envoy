#include "common/stats/stats_impl.h"

#include <string.h>

#include <chrono>
#include <string>

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

namespace {

// Round val up to the next multiple of the natural alignment.
// Note: this implementation only works because 8 is a power of 2.
size_t roundUpMultipleNaturalAlignment(size_t val) {
  const size_t multiple = alignof(RawStatData);
  return (val + multiple - 1) & ~(multiple - 1);
}

} // namespace

size_t RawStatData::size() {
  return roundUpMultipleNaturalAlignment(sizeof(RawStatData) + nameSize());
}

size_t& RawStatData::getMaxNameLength(size_t configured_size) {
  // Like CONSTRUCT_ON_FIRST_USE, but non-const so that the value can be changed by tests
  static size_t size = configured_size;
  return size;
}

void RawStatData::configure(Server::Options& options) {
  const size_t configured = options.maxStatNameLength();
  RELEASE_ASSERT(configured > 0);
  size_t max_name_length = getMaxNameLength(configured);

  // If this fails, it means that this function was called too late during
  // startup because things were already using this size before it was set.
  RELEASE_ASSERT(max_name_length == configured);
}

void RawStatData::configureForTestsOnly(Server::Options& options) {
  const size_t configured = options.maxStatNameLength();
  getMaxNameLength(configured) = configured;
}

std::string Utility::sanitizeStatsName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  return stats_name;
}

void TimerImpl::TimespanImpl::complete(const std::string& dynamic_name) {
  std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_);
  parent_.parent_.deliverTimingToSinks(dynamic_name, ms);
}

RawStatData* HeapRawStatDataAllocator::alloc(const std::string& name) {
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
