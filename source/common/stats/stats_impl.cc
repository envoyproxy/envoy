#include "stats_impl.h"

namespace Stats {

void TimerImpl::TimespanImpl::complete(const std::string& dynamic_name) {
  std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - start_);
  parent_.parent_.deliverTimingToSinks(dynamic_name, ms);
}

RawStatData* HeapRawStatDataAllocator::alloc(const std::string& name) {
  raw_data_list_.emplace_back(new RawStatData());
  raw_data_list_.back()->initialize(name);
  return raw_data_list_.back().get();
}

void RawStatData::initialize(const std::string& name) {
  ASSERT(!initialized());
  ASSERT(name.size() < MAX_NAME_SIZE);
  strncpy(name_, name.substr(0, MAX_NAME_SIZE).c_str(), MAX_NAME_SIZE + 1);
}

bool RawStatData::matches(const std::string& name) {
  // In case a stat got truncated, match on the truncated name.
  return 0 == strcmp(name.substr(0, MAX_NAME_SIZE).c_str(), name_);
}

} // Stats
