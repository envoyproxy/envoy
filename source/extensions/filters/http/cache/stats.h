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
  virtual void incActiveCacheEntries() PURE;
  virtual void decActiveCacheEntries() PURE;
  virtual void incActiveCacheSubscribers() PURE;
  virtual void subActiveCacheSubscribers(uint64_t count) PURE;
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

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
