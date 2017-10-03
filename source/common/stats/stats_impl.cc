#include "common/stats/stats_impl.h"

#include <string.h>

#include <chrono>
#include <string>

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

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
