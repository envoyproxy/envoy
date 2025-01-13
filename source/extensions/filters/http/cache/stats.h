#pragma once

#include <memory>

#include "source/extensions/filters/http/cache/cache_entry_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterStats {
public:
  virtual void incForStatus(CacheEntryStatus status) PURE;
  virtual ~CacheFilterStats() = default;
};

using CacheFilterStatsPtr = std::unique_ptr<CacheFilterStats>;

CacheFilterStatsPtr generateStats(Stats::Scope& scope, absl::string_view label);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
