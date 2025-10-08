#pragma once

#include <memory>

#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheFilterStats {
public:
  virtual void incForStatus(CacheEntryStatus status) PURE;
  virtual void incCacheSessionsEntries() PURE;
  virtual void decCacheSessionsEntries() PURE;
  virtual void incCacheSessionsSubscribers() PURE;
  virtual void subCacheSessionsSubscribers(uint64_t count) PURE;
  virtual void addUpstreamBufferedBytes(uint64_t bytes) PURE;
  virtual void subUpstreamBufferedBytes(uint64_t bytes) PURE;
  virtual ~CacheFilterStats() = default;
};

class CacheFilterStatsProvider {
public:
  virtual CacheFilterStats& stats() const PURE;
  virtual ~CacheFilterStatsProvider() = default;
};

using CacheFilterStatsPtr = std::unique_ptr<CacheFilterStats>;

CacheFilterStatsPtr generateStats(Stats::Scope& scope, absl::string_view label);

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
